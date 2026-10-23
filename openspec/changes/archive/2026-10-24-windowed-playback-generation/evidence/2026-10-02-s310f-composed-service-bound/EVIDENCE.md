# Slice 3.10 (increment 6) — the starvation half, the composed progress bound, and a measured contradiction in the declared envelope

**Status:** landed 2026-10-02 on `feature/windowed-playback-generation`, **test-only** (no production line changed). Task
**3.10 stays open** — this increment closes its **starvation half** and, by measurement, sharpens its **envelope
derivation** item into a concrete, numeric problem.

## What the task asked for

> "... capture finalization and persistence keep their own opportunities (no starvation); one declared grain per admitted
> turn, no hidden stacking. Verify with a fixture that keeps an early holder blocked while later holders need preparation
> and capture/persistence work is pending, **asserting a progress bound for every admitted holder** (including inbound
> switch preparation and overlapping finalization), and **derive the supported envelope from grain costs and the worst
> permitted service gap — invent no microsecond budget**"

## What was already there (so this increment's own value is not overstated)

| Property | Where it was already established |
|---|---|
| the owner rotation is unconditional and exact — one turn per owner per cycle, **the lane included** | `test_midi_service_drain::test_select_deferred_idle_owner_backlog_snapshot_two_of_eight` (the exact 16-turn sequence, with the lane as one of the seven) |
| the lane serves **one declared grain per call** | `test_runtime_working_set_ownership::test_window_owner_serves_one_grain_per_turn_and_publishes_nothing` (`expectedTurns == 5`: "a grain served twice in one turn, or a grain skipped, changes this number") |
| a holder that cannot progress never consumes the rotation and is served once able | increment 1's holder fixture |
| the lane is **never denied** its turn | `deferredIdleOwnerDependsOnVisualCacheWrap` covers `VisualCache` and `LoadAndDisplay` only |

And the caller's shape, **build-verified rather than fixture-proved** (`src/main.cpp` is not native-linkable):
`SlotProvision` and `PlaybackViewMaintenance` are served **before** the stopped/playing guards, and **every owner branch
ends in `return`** — so one owner's turn cannot spill into another's, and the lane's branch calls
`stepPlaybackViewMaintenance` **once**.

## What this increment adds

1. **A composed-service fixture** (`test_window_service_keeps_one_turn_per_rotation_and_no_owner_loses_a_turn`): the
   **production selector** picks one owner per idle turn while the lane owes two windows, and the lane's turns serve its
   grain — the composition the task's sentence describes, which no fixture did before.
   - **(A) No starvation and no extra share:** every one of the seven owners — the lane included — is served **exactly
     once per rotation** (`Expected 60 Was 1` under M95).
   - **(B) The lane progresses to completion** inside the measured bound, with **one completion per owed window**, and the
     measured lane-turn figure pinned (a stacked or skipped grain moves it).
   - **(C)** the finding below, **asserted** so it cannot be forgotten.
2. **The measurement**, which is where the value is.

## The finding: the declared envelope does not bound this work shape (measured, numeric)

| Quantity | Value |
|---|---|
| work shape | one 64-bar loop, two owed windows carrying **81 notes** between them |
| **measured** | **58 grains, 60 lane turns, 2 completions** |
| the contract the owner is admitted under declares | `requiredFillGrains 3` + `requiredOrderGrains 3` + `requiredPromotionGrains 1` = **7 grains**; `maximumConsecutiveDeniedTurns 3` → worst case **28 turns** |
| the declared quanta | fill `kPlaybackViewFillQuantum = 64`, order `kPlaybackViewOrderQuantum = 48` |
| the window's declared event capacity | `RuntimePlaybackViewConfig::kEventsPerView = 2048` |

So two declarations in the same contract disagree by roughly an order of magnitude: `3` order grains at `48` entries
describes a window of **≤ 144 ordered entries**, while the window is declared to hold up to **2 048**. Measured against
real work, the cost sits with the work, not with the declaration (58 grains for 81 notes and their interval).

**Why it matters, in the contract's own currency.** The admission decision (`canCompleteBeforeHandoff`) judges the
handoff lead with the **declared** grains and denied-turn allowance — the declaration produces the 29-bar lead
(22 272 ticks) that the whole family already uses (`kWindowOwnerExpected*`, 4.4's derived 30-bar floor). A shape that
needs 60 lane turns needs `60 × 768 = 46 080` ticks ≈ **60 bars** before its handoff. Such a shape can therefore be
**admitted** and still miss its handoff. The one-pass-late rule is the designed answer to a missed deadline — the
consumer is never blocked — but the *declared* envelope is not supported for that shape, and the task's own wording
("derive the supported envelope from grain costs and the worst permitted service gap") is exactly the repair.

**The derivation is not invented here.** Assertion (C) pins the *violation* deliberately: deriving the envelope properly
is expected to **invert** it, which forces this record to be updated with the new numbers instead of letting the
inconsistency live in prose.

## Red-proofs

| # | Mutation | Observed failure (measured) |
|---|---|---|
| M95 | the selector gives the lane an **extra turn per rotation** (the plausible "the lane owes a lot" priority hack) | `Expected 60 Was 1. every deferred-idle owner keeps exactly one turn per rotation` — the hacked selector starves one owner down to a single turn, which is precisely the property |
| M96 | the lane's fill grain serves **twice the declared quantum** | `Expected 60 Was 59. the owed windows were done in the measured number of lane turns` — and 3.1's grain fixture and 3.2's atomic-publication fixture fail with it |
| M97 | a lane turn **skipped** every other turn | 18 cases fail, so the lane's properties are sensitive to a skipped turn rather than tolerant of it |

Anchors had their occurrence count asserted to be 1, and each restored **byte-identically** (drain sha256
`1c019eb0…b5c913`, build sha256 `d79ad359…c5674c7` before and after all three).

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **71/71** (+1 fixture) |
| `pio test -e native` (full) | **2706 cases: 7 skipped, 2699 succeeded, 0 failed** (baseline 2705 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** in **2.77 s** — a **relink, not a rebuild** (this increment changes no production line, so the RAM1 line cannot have moved); **byte-identical** `variables 96,192 / code 392,328 + padding 888`, 48 B headroom, **floor not re-based** |

## Not claimed

- **The other owners' work per turn is not executed here.** The fixture proves the *allocation* (one turn per owner per
  rotation); what each owner does inside its turn is the caller's business and stays build-verified.
- **The cost model is not derived.** This is **one** measured shape (two windows, 81 notes, a 64-bar loop), and the
  breakdown that matters (how fill and order costs scale with source size, interval length and entry count) is **not**
  established. The fixture pins what it measured; the systematic sweep is owed.
- **The blocked-holder case is the sibling fixture's**, not this one: here the lane owes two windows, neither blocked.
- **No device observation**, and the lane still has **no production caller** — the composed loop here is the production
  composition driven natively, not the production loop.
