# Slice 3.10 (increment 8) — the source-lifetime repair, the fairness repair, and the derived admission

**Status:** landed 2026-10-02 on `feature/windowed-playback-generation`. It repairs **two defects in my own earlier
increments**, corrects **three claims in the increment-7 report**, and lands the **derivation** the review asked for
**without enabling** it — the reason is measured and recorded below.

## 1. The source-lifetime defect (mine; repair, with proof that it invalidated measurements)

`prepareOwingWindowHolder` gathered into a **stack-local** `SessionMidiEventVec` and returned, while
`RuntimePlaybackViewFill::step` keeps walking `cursor_` over `source_[cursor_]` **across turns** — so every turn after the
helper returned read a source that had died with its frame. The build's *source* is the caller's gather buffer (the
window's events, not the loop), so this was dangling memory, not a copy.

**The proof that it invalidated measurements is that repairing it changed them.** Three fixtures' outcomes moved as soon
as the source became valid:

| Fixture | Before (dangling) | After (valid) |
|---|---|---|
| the composed-service fixture | two completions inside its envelope | one window could not finish inside the (mis-derived) envelope |
| the cost sweep, sparse window | **1** turn | **2** turns |
| `test_window_owner_serves_the_most_urgent_holder_and_rotates_between_them` cases (B) and (C) | "passed" | **not discriminating at all** — see §4 |

**Repair:** `windowSourceScratch(slot)`, a file-scope **per-slot** buffer (two slots can owe at once), replaced only by a
later request for the same slot — which the fixtures issue only after that slot's earlier build completed. The contract
this expresses is the real one: *the caller owns the source for the build's whole duration*.

## 2. The holder-fairness defect (mine; repaired against the spec's own words)

The spec: *"Service SHALL **rotate round-robin across eligible holders**, and **within a holder** SHALL serve the earliest
outstanding playback deadline first"*, with the scenario *"Earlier deadlines win within a holder — WHEN **a holder has
more than one outstanding playback deadline**"*. The design names the same order: *"**2 — Fairness, then deadline order.**"*

`selectPlaybackHolder` applied earliest-deadline-first **globally** — and my own doc comment even chose that reading
("the tighter of the two readings"). It starves: a holder whose deadline is permanently earlier than another's is chosen
on **every** rotation, and the cursor cannot rescue the loser, because the deadline comparison ignores where the scan
starts.

**Repair:** `selectPlaybackHolderRoundRobin` is the **across-holders** rule (the cursor decides *who*); `selectPlaybackHolder`
keeps the **within-a-holder** role with its doc corrected at the source; `src/main.cpp`'s track choice now uses the
round-robin form (the per-track offer still picks that track's own most urgent holder, which is the within-holder half).
**Proof:** new fixture `test_track_service_rotates_so_no_track_is_starved_by_anothers_earlier_deadline` drives the
production composition with two tracks whose deadlines really are unequal and whose near obligation is **long-lived**
(80 notes, dozens of grains, so the defect cannot hide behind a build that finishes first) and asserts both tracks are
served within one rotation. Under the old rule the near track took every turn.

## 3. The derived admission (implemented; deliberately **not** enabled)

`applyWindowCostToContract(contract, windowEventCount)` is the single source of truth: fills are
`ceil(events / kPlaybackViewFillQuantum)`, order grains are `RuntimePlaybackOrderBuilder::requiredGrains(events,
kPlaybackViewOrderQuantum)` — the builder's **own** worst-case entry count, `events × (1 + 2·⌈log₂events⌉)` divided by
the quantum — plus one promotion grain. It is used for the contract's basis window **and** available per request.

**Corrected cost model (measured after the lifetime repair, valid sources):**

| notes | **events** | fills | orders | turns | helper bound |
|---|---|---|---|---|---|
| 1 | 2 | 1 | 1 | 2 | 1 |
| 10 | 20 | 1 | **1** | 2 | **5** |
| 20 | 40 | 1 | **11** | 12 | **11** |
| 40 | 80 | 2 | **25** | 27 | **25** |
| 80 | 160 | 3 | **57** | 60 | **57** |
| 160 | 320 | 5 | **127** | 132 | **127** |
| 80 (32-bar loop) | 160 | 3 | 57 | 60 | 57 |
| 80 spread | 6 | 1 | 1 | 2 | 1 |

So: **events = 2 × notes** (the increment-7 report's "window events" column was notes); **fills = ⌈events/64⌉ exactly**;
and **orders equal the helper exactly where a merge runs**, falling *below* the bound where the input is already ordered
(20 events: bound 5, measured 1). The increment-7 "threshold near 10 events" was that skip, not a size threshold, and its
2 048-event extrapolation (~1 600 grains) was wrong: the helper gives **982 order + 32 fill**.

**Why it is not enabled, measured.** With the *declared* `maximumPhaseAdvancePerTurn` (one bar per owner turn) and the
rotation-derived gap, an 80-note window's derived need is 61 grains × 7 turns × 768 ticks ≈ 328 000 ticks ≈ **428 bars** —
longer than any plausible loop, so enabling it **refuses the shapes the current constants admit**. That is exactly the
capacity reduction the review said not to approve on unmeasured evidence, so enabling it lands together with re-based
fixtures and, ideally, the measured gap. The call site carries that reasoning; the one-line change is documented.

**The service-gap bound, split honestly in two:**

- **the rotation half is proven** — the exact one-turn-per-owner-per-rotation sequence, and now the across-holders
  fairness fixture: a holder that can progress is served once per `kDeferredIdleOwnerCount` turns.
- **the ticks-per-turn half is not** — `maximumPhaseAdvancePerTurn` remains the *declared* one bar, unmeasured. The sweep
  fixture now asserts that fact as a statement, so nobody can read the envelope as supported: *"a supported deadline
  envelope still requires a proven service-gap bound — not merely larger constants."*

## 4. Assertions removed because they could not fail

Cases (B) and (C) of `test_window_owner_serves_the_most_urgent_holder_and_rotates_between_them` asserted which holder
advanced using `windowHolderAdvanced`, which **cannot distinguish a served holder whose fill spans several grains from an
unserved one** (a 158-event window reads as unserved either way). Measured: the old form passed only because the dangling
source had made that slot stop owing entirely. Both are removed with the reason recorded — an assertion that cannot fail is
not evidence — and the rotation claim now rests on the policy's own inputs plus the new fairness fixture.

## 5. Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **73/73** (+1 fairness fixture; −1 diagnostic that became the real one) |
| `pio test -e native` (full) | **2708 cases: 7 skipped, 2701 succeeded, 0 failed** |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — **true rebuild** (82.6 s, cache cleared); RAM1 line **byte-identical**: `variables 96,192 / ITCM 393,216 (code 392,328 + padding 888) / locals 34,880`, 48 B headroom, **floor not re-based** |

## Not claimed

- **The admission derivation is implemented, not enabled** (§3), so the *behaviour* of admission is unchanged in this
  commit.
- **No device observation**, and the lane still has no production caller.
- **The rotation is what bounds a holder's gap natively;** the ticks that pass during those turns are a device fact and
  remain unmeasured — the remaining half of the service-gap bound.
- **`kWindowAdmissionLimit = kEventsPerView = 2048` is untouched:** this increment removes no capacity and adds none; it
  makes the *cost* of a window computable, and records where the capacity and the service envelope disagree.
