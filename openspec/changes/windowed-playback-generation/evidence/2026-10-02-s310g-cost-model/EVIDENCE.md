# Slice 3.10 (increment 7) — the measured grain-cost model, and the supported window derived from it

**Status:** landed 2026-10-02 on `feature/windowed-playback-generation`, **test-only** (no production line changed). This
is the increment increments 6 and 2 both pointed at: 3.10's item "**derive the supported envelope from grain costs and the
worst permitted service gap — invent no microsecond budget**". The envelope is now **derived from a measurement**, and the
measurement contradicts two declared numbers.

## The measurement

One owed window per sample, driven to completion by the lane alone; `nextCoverageBuildGrain()` is read **before** each turn,
so the fill/order split is the owner's own answer rather than an inference.

| window events `N` | fills | orders | lane turns | turns × `maximumPhaseAdvancePerTurn` |
|---|---|---|---|---|
| 1 | 1 | **0** | **1** | 768 ticks (1 bar) |
| 10 | 1 | **0** | **1** | 768 |
| 20 | 1 | 11 | 12 | 9 216 (12 bars) |
| 40 | 2 | 25 | 27 | 20 736 (27 bars) |
| 80 | 3 | 57 | 60 | 46 080 (60 bars) |
| 160 | 5 | 127 | 132 | 101 376 (132 bars) |

Controls (same run): **80 notes on a 32-bar loop cost byte-identically to 80 notes on a 64-bar loop** (60 turns, 57
orders), and **the same 80 notes spread across the loop** — so the window holds a handful — **cost one turn**.

## The model, as measured

1. **The cost is a function of the window's event count alone.** Not the loop's length, not the source's size: the 64-bar
   and 32-bar loops are identical, and content outside the window is free (`spread` = 1 turn).
2. **There is a threshold.** At ≤ 10 events the build completes in **one** grain with **no order grain at all**.
3. **Above it the order pass dominates and grows super-linearly:** order grains 11 → 25 → 57 → 127 as `N` doubles
   (×2.27, ×2.28, ×2.23), i.e. a per-note cost that *rises* with `N` (0.55 → 0.79 grains/note). A **linear** grain count
   in the window's entries cannot describe it.

## The derived bound (no microsecond budget invented)

A lane turn advances the playhead at most `maximumPhaseAdvancePerTurn`, so a window's service need is
`turns(N) × maximumPhaseAdvancePerTurn` **ticks of playhead travel** — ticks the loop must have left before the handoff:

- **64-bar loop:** `N = 80` needs 46 080 ≤ 49 152 ticks ✓ servable within the loop; `N = 160` needs 101 376 > 49 152 ✗.
- **32-bar loop:** `N = 80` needs 46 080 > 24 576 ✗ — **and the request was nevertheless admitted** (the fixture's own
  helper asserts `Admitted`).

## The contradictions this makes numeric

| Declared | Measured | Gap |
|---|---|---|
| `requiredFillGrains 3` + `requiredOrderGrains 3` + `requiredPromotionGrains 1` = **7 grains** at quanta 64/48, with a 3-denied-turn allowance → worst case **28 turns** | **60 turns** for a window of 80 events | the declaration covers roughly **`N` ≈ 5–10**, not the capacity |
| the declared lead that arithmetic produces: **22 272 ticks ≈ 29 bars** (and 4.4's derived **30-bar floor**) | an admitted 80-event window on a 32-bar loop needs **46 080 ticks ≈ 60 bars** | the admission can admit a shape it cannot service before the handoff |
| `kWindowAdmissionLimit = kEventsPerView = 2048` (3.12c) | extrapolating the measured per-note cost, `N = 2048` needs on the order of **1 600 order grains ≈ 1 600 lane turns ≈ 1 600 bars** of playhead travel | the admission limit is ~an order of magnitude beyond any admissible loop |

The one-pass-late rule is the designed answer to a missed deadline — no consumer is blocked, and the containment path
(4.5) covers the fault — but the **declared** envelope is not supported for these shapes, exactly as 3.10 requires.

## The reconciliation: preferred path stated, deliberately **not** applied

**Preferred: derive both numbers from the measured cost, keeping the grain quanta.** The quanta (fill 64 / order 48) are
a deliberate *timing* choice (bounded work per off-clock turn, AC-6) with a measurement behind them; `kEventsPerView =
2048` is a *capacity* with no measured basis and is the side the service cannot honour. So:
(i) the contract's declared grain counts become a function of the window's event count rather than constants, and
(ii) the admitted window's event count (3.12's `kWindowAdmissionLimit`) is derived from the same envelope and the loop's
own length. **Confidence:** high that the capacity is the wrong side to keep (three independent measurements agree);
**medium** on the exact functional form, because the super-linear *shape* is measured but its constant is fitted loosely
from four points, and the order pass's own quantum/algorithm is not read here.

**Not applied, and why:** changing `requiredFillGrains`/`requiredOrderGrains` moves the declared lead (22 272 ticks /
29 bars), 3.12's admission limit and 4.4's derived 30-bar floor, and it would **refuse shapes that are admitted today** —
a contract change with behavioural effect, which is the user's call rather than a silent re-base. Assertion (D) in the new
fixture is written as the *contradiction*, so applying this reconciliation is expected to **invert** it.

## Red-proofs

| # | Mutation | Observed failure (measured) |
|---|---|---|
| M98 | the order grain serves twice the declared quantum (48 → 96) | `Expected 11 Was 6` on the pinned table — the pins are load-bearing — and 3.1's grain fixture and 3.2's publication fixture fail with it |
| M99 | the build always enters the order phase (`phase_ = Phase::Order`) | `Expected 1 Was 2` on the sparse-window assertion — the threshold assertion speaks — plus the composed fixture's completion count and the urgency fixture |
| M100 | the **declared** `maximumPhaseAdvancePerTurn` inflated ×100 | the derivation's equality pin fails (`60 × 768` no longer equals the service-need figure), i.e. **(D) reads the declared contract field, not a literal**; 20 cases fail in total, 4.4's floor sweep among them |

Anchors had their occurrence count asserted = 1 and each restored **byte-identically** (window-build TU sha256
`d79ad359…c5674c7`, view-build TU sha256 `c752601e…a31717` before and after all three).

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **72/72** (+1 fixture) |
| `pio test -e native` (full) | **2707 cases: 7 skipped, 2700 succeeded, 0 failed** (baseline 2706 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** in 3.32 s — a **relink, not a rebuild** (test-only); RAM1 line **byte-identical** (`variables 96,192 / code 392,328 + padding 888`), 48 B headroom, **floor not re-based** |

## A fixture bug of my own, recorded

The super-linearity assertion was first written **backwards** (`forty.orders * 2 > eighty.orders` → 50 > 57 is false) and
the run caught it; the corrected form (`eighty.orders > forty.orders * 2`) is what the data says (×2.27). The pinned
values themselves passed on the first run, which is how the direction error was isolated to the inequality rather than
the measurement.

## Not claimed

- **The model's constant is not derived.** The *shape* (window-content only, a threshold near 10, super-linear above it) is
  measured; the exact law and its constant are not, and four points do not establish a curve. The systematic sweep across
  `N` (and the order pass's own quantum) is still owed.
- **The reconciliation is proposed, not applied** (above) — the declared grains, the declared lead, 3.12's admission
  limit and 4.4's floor are all untouched by this increment.
- **The samples are the fixture's own shape** (a record pass with a spread of 48-tick notes; a 2-bar centred window;
  a single owing slot). Other content shapes (chords, long notes, dense same-tick runs) are not measured.
- **No device observation**, and the lane still has **no production caller**.
