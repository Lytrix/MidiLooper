# Slice 3.10 (increment 9) — the service-gap unit pinned, and increment 8's bound retracted as non-general

**Status:** landed 2026-10-02 on `feature/windowed-playback-generation`. It corrects **my own increment-8 record**; the
review that caught it is right on both counts.

## The error

Increment 8 reported *"an 80-note window's derived need is ≈ 428 bars"*. That figure was **wrong twice**:

1. **It double-counted the owner rotation.** `maximumPhaseAdvancePerTurn` is declared *per this owner's own opportunity* —
   a `PlaybackViewMaintenance` dispatch — which **already contains** the other six deferred owners' share of the 7-owner
   rotation. Multiplying the grains by `kDeferredIdleOwnerCount` as well counts that share a second time.
2. **It omitted the contention inside the lane.** The lane's own opportunity is not "one per holder":
   - one **track** of every `H` eligible tracks is served per lane turn (`src/main.cpp`'s offer round-robin, bounded by
     `kWindowHolderOffers = 8` and by `Config::NUM_TRACKS`);
   - within the chosen track, the sibling slots contend too — and the spec's **within-holder earliest-deadline-first**
     means a sibling whose interval is earlier may legitimately take that turn.

So 428 bars was a **single-track, single-slot** figure presented as a general per-window bound.

## The repair

- **The unit is pinned where the field is declared** (`CoverageServiceContract::maximumPhaseAdvancePerTurn`): the phase the
  playhead advances between two of **this owner's** opportunities; what it contains (the other owners' share) and what it
  does **not** (`H` eligible tracks × `K` sibling slots) are named there, with the consequence spelled out:
  `canCompleteBeforeHandoff`'s `grains × (denied + 1)` is **not yet a complete bound**. The unit must be *measured*, and
  the contention envelope *declared*, before any envelope may be called supported.
- **The production offsets are named from the production shape**, not guessed:
  `kDeferredIdleOwnerCount × Config::NUM_TRACKS × Config::MAX_LOOPS_PER_TRACK` (with `main.cpp`'s 8-offer bound noted as
  the smaller divisor where it applies).
- **The sweep fixture asserts the structure instead of a fabricated figure:** the measured cost of an 80-note window
  (**61 grains** = 3 fills + 57 orders + 1 promotion — a real measurement, unchanged), the **still-declared** one-bar
  advance, and the fact that the admission's turn count (`denied + 1`) is **smaller than the contention factor** — i.e.
  the bound is **incomplete by construction**, not merely large. Asserting the incomplete structure is the honest form:
  no bar figure can be computed until the unit exists.

## Consequence, recorded rather than papered over

**Leaving cost enforcement disabled does not make the current admission safe.** The admission is *unsupported*: it
multiplies by an unmeasured unit and omits the lane's internal contention. Active windowed playback therefore stays
**gated** — which is what `docs/Authority/Architecture/ExecutionBounds.md` row 27 already records (caps declared, bounds
unmeasured) and what task 3.7's block depends on. This increment removes no capacity and enables nothing.

## What the next step needs, in order (the review's own recommendation)

1. **Define the unit** — done here at the declaration, in words; the *value* still has to be measured (device): the phase
   the playhead advances between two `PlaybackViewMaintenance` opportunities.
2. **Declare the supported contention envelope** — which `H` (eligible tracks) and `K` (sibling windows) the supported
   configuration bounds, since the worst case (`NUM_TRACKS × MAX_LOOPS_PER_TRACK`) would refuse everything.
3. **Then** collect device data and derive the envelope from it; **then** re-base the fixtures and enable the derived
   admission (the one-line change documented at the call site).

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **73/73** |
| `pio test -e native` (full) | **2708 cases: 7 skipped, 2701 succeeded, 0 failed** |
| `pio run -e teensy41-capture-serial` | **SUCCESS**; RAM1 line **byte-identical** (`variables 96,192 / code 392,328 + padding 888`), **floor not re-based** |

## Not claimed

- **No bar figure is claimed** for any window: that is the point of the correction.
- **The admission is not safe**, and windowed playback stays gated (above).
- **The measured cost model is unchanged and still stands** (events = 2 × notes; fills = ⌈events/64⌉; orders =
  `RuntimePlaybackOrderBuilder::requiredGrains` where a merge runs) — this increment corrects the *envelope* arithmetic,
  not the costs.
