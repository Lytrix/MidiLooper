# Slice 3.10 — fair, bounded, deadline-ordered service at the holder level (DEC-108 §13b)

**Change:** `windowed-playback-generation` · **Task:** 3.10 (**landed, first increment; the task stays open on the
wiring and on the starvation/envelope halves**) · **Date:** 2026-10-02 · **Branch:**
`feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Increment 3.10
**Contract:** design §13b — "Playback maintenance gets a recurring, **bounded** service opportunity; holders rotate
round-robin; within a holder the **earliest outstanding playback deadline** is served first; the selection cursor
advances **even when a holder cannot progress**, so a blocked early track cannot monopolise service; capture
finalization and persistence keep their own explicit opportunities and playback urgency must not starve them; one
declared grain per admitted turn with no hidden stacking", with the envelope **derived** from grain costs and the worst
permitted service gap. Finding **A** of the same review is the tightening this starts from:
`servicePendingSlotProvision` serves the first pending track and returns.

---

## What landed

| File | Change |
|---|---|
| `include/Utils/MidiServiceDrain.h` | `PlaybackHolder` (eligible / can progress / outstanding deadline), `PlaybackHolderCursor`, the named `PlaybackHolderSelection { None, Service, AllBlocked }`, `playbackHolderDeadlineIsEarlier` and the pure `selectPlaybackHolder(cursor, holders, count, out)`. Header-only, no state beyond the caller's cursor, no allocation. |
| `test/test_midi_service_drain/…` | `test_playback_holder_service_is_fair_deadline_ordered_and_never_held_by_a_blocked_holder` — the four properties as bounds, five blocks. |

**Why the holder level is the gap, verified rather than assumed.** The **owner** rotation already exists and is already
proved: `nextDeferredIdleOwner` cycles unconditionally *before* eligibility is known, and
`test_select_deferred_idle_owner_backlog_snapshot_two_of_eight` already asserts "every owner but Snapshot gets exactly
one turn per cycle". What did **not** exist is the level below it: which *holder* (one slot's owed build) an admitted
`PlaybackViewMaintenance` turn serves. There, the scan is ascending first-match with no cursor
(`Track::stepPlaybackViewMaintenance`), and **no deadline is stored at all** — `beginCoverageBuild` takes
`handoffPhase` and drops it, so "earliest outstanding playback deadline" had no value to order by.

**The deadline is not new state.** It is the phase at which the playhead next enters the interval the build serves — the
figure `Track::requestPlaybackWindow` already computes and passes as `handoffPhase`. The policy therefore reads an
answer the caller already has instead of introducing a second currency.

**Reading chosen, and why:** earliest-deadline-first is applied **globally** over the eligible set, which is the tighter
of the two readings of "within a holder" — a global order cannot serve a later deadline first inside any one holder, so
the per-holder reading follows a fortiori, and the property is stated once rather than per holder.

## The oracle

| Block | Claim |
|---|---|
| **A — the progress bound** | with every holder eligible and able, `count` consecutive selections serve each holder **exactly once** (no holder twice before another once), and the following rotation continues from the cursor rather than restarting at zero |
| **B — earliest deadline first** | deadlines are deliberately the reverse of the holder order, so an ascending scan answers wrongly; the served order is the deadline order |
| **C — where urgency cannot distinguish** | a holder with an outstanding deadline outranks every holder without one; deadline-less holders still rotate **evenly** among themselves (fairness survives) |
| **D — a blocked holder never holds the position** | with the early holder blocked, it is never chosen, the others still keep the one-rotation bound, and once it can progress again it is served **within one rotation** — skipped over, never lost |
| **E — the outcomes are named** | "nothing owed" is `None` (distinct from "all owed and blocked" = `AllBlocked`), `holderOut` is `kNoPlaybackHolder` in every non-`Service` outcome, and the rotation **advances** in both non-serving cases (never a latch); `nullptr`/zero-count spans are `None` |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_midi_service_drain` | **39/39** (was 38; +1 fixture) |
| `pio test -e native -f test_runtime_working_set_ownership` | **67/67** (unchanged) |
| `pio test -e native -f test_storage_loop_io` | **52/52** (unchanged) |
| `pio test -e native -f test_playback_prewarm` (cited pin) | **190/190** (unchanged) |
| `pio test -e native` (full) | **2700 cases: 7 skipped, 2693 succeeded, 0 failed** (baseline 2699 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** (36.7 s: the header's dependents recompiled) — `variables 96,192` **unchanged** / `code 392,328 + padding 888` / locals 34,880, floor 840, **48 B headroom**: the policy is inline and unconsumed, so it emits no code |

## Red-proofs

Four mutations, each applied to the **named** header at an anchor whose **occurrence count** was asserted before the
run, each restored **byte-identically** (`shasum -a 256 -c`):

| # | Mutation | Observed failure |
|---|---|---|
| M69 | the scan ignores the rotation cursor (fixed ascending order) | `no holder may be served twice inside one rotation` |
| M70 | a holder that cannot progress is served anyway | `a holder that cannot progress must never be chosen` |
| M71 | the deadline order is inverted | `Expected 1 Was 0. the earliest outstanding deadline is served first` |
| M72 | the cursor resumes **on** the served holder instead of after it | `no holder may be served twice inside one rotation` |

## Not claimed

- **The policy has no production caller yet.** The holder scan in `Track::stepPlaybackViewMaintenance` (and the track
  scan in `src/main.cpp`, which is **not native-linkable**) still uses ascending first-match, and nothing carries the
  outstanding handoff phase per holder. That wiring is this task's next increment and is what turns the policy from a
  proved rule into a served one.
- **The starvation half is not implemented.** "Capture finalization and persistence keep their own explicit
  opportunities" is, today, *structural* only — the capture-stop owner takes whatever turn arrives
  (`Track::processDeferredIdleMaintenance`) and persistence runs from a non-rotation gate — and no progress bound is
  asserted for either. That owner lives in `src/main.cpp` and its track-level body, so it needs its own increment.
- **The envelope is not derived here.** §13b asks the supported envelope to be derived from grain costs and the worst
  permitted *gap between service opportunities*; this slice states the per-holder bound in **turns** and names the gap
  measurement as owed — it is the same unmeasured `maximumPhaseAdvancePerTurn` the coverage contract already flags, and
  task 6.3's sweeps plus 3.10's own derivation own it. No microsecond budget is invented.
- **"One declared grain per admitted turn" is not asserted here.** It holds per owner today (the window owner serves one
  grain and breaks the scan; the canonical prefix one child), but `DeferredIdleOwner::Snapshot` declares two units per
  turn and `Track::processDeferredIdleMaintenance` runs several children per owner call, so a uniform invariant is not
  there to assert.
- **Task 3.10 stays open** on all four of the above, and `ExecutionBounds.md` row 27 still classifies active windowed
  playback as **not admitted**.
