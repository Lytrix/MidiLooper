# Slice 4.4 — length-independent residency, swept over loop lengths (F-8)

**Change:** `windowed-playback-generation` · **Task:** 4.4 (**landed; the task stays open on its parity half and on the
device leg**) · **Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Increment 4.4
**Contract:** the spec's *A window is served without whole-root residency* and *Length-independent root residency* —
"A window SHALL be servable while the loop's whole root is not resident, and content parity SHALL hold for that demoted
state … **no whole-take reservation SHALL be restored for demoted slots** to achieve it"; design **D5/D6** and the
risk line "F-1/F-8 assert identity, retention and **residency**, not naming; **F-8 is swept over loop lengths**".

---

## What landed

**Test-only. No production code changed** — the residency statement is read from the slot's own accounting through the
existing test seam, and every setup step is a production entry.

| File | Change |
|---|---|
| `test/test_runtime_working_set_ownership/…` | `test_demoted_slot_residency_is_length_independent_swept_over_lengths` — the sweep, over **16, 17, 24, 30, 32 and 64 bars**, one slot per length. |

Per length the fixture sets the loop up through production entries only — `Loop::fullTakePlaybackViewAdmissible()` as
the policy and `Track::prewarmPlaybackForSlot()` as the admission (which decides the representation *itself*, from the
loop, so the fixture never states the policy to the code) — then reads the slot's residency from
`TrackCaptureStopTestAccess::runtimeIfAllocated(...)->viewSetIfPrewarmed()`: `wholeTakeViewsReserved()`,
`fixedExternalBytes()`, the views' storage capacities and `captureStagingsPrewarmed()`.

## The oracle, swept

| Claim | Observed |
|---|---|
| **the policy boundary** | admissible exactly at the declared bar cap (`kMaxDetailedWindowBars` = 16 bars) and not one bar past it: 16 ✓, 17 ✗ |
| **"no whole-take reservation for demoted slots"** | `wholeTakeViewsReserved()` is true **exactly** while admissible and **false at every demoted length** (17, 24, 30, 32, 64) |
| **length independence (the F-8 claim)** | every demoted length reports the **same** `fixedExternalBytes()` — the windowed reservation is a function of the declared views and stagings, never of the loop's length — and it is **strictly smaller** than the admissible figure, so the whole-take bundle is genuinely absent rather than merely unused |
| **the window term is the declared one** | every view's storage capacity is `RuntimePlaybackViewConfig::kEventsPerView` at **every** length (the requested companion-row horizon does not leak into the window's capacity) |
| **the stagings stay** | the capture stagings are resident at every length |
| **windows resident, root demoted** | at and above the declaration's floor the request is `Admitted` and the slot is then **served from a window** (`playbackWindowServesSlot`) while its whole take is not reserved |
| **the floor, as a number** | the request's answer equals the declaration's own `canCompleteBeforeHandoff` at every length, *and* the fixture re-derives the lead from the contract's declared fields (`requiredGrains × (maximumConsecutiveDeniedTurns + 1) × maximumPhaseAdvancePerTurn + leadTicks`) and requires the sweep's answer to be exactly that arithmetic. Below it the refusal is **named** (`CannotCompleteBeforeHandoff`), nothing is half-prepared, and no window is served. |

**Measured consequence, recorded rather than smoothed over:** with the declaration's conservative
`maximumPhaseAdvancePerTurn` (one bar, the *unmeasured* term the code itself flags), the required lead is
**22,272 ticks ≈ 29 bars**, so a demoted loop shorter than that can never have a window prepared: **the floor is
exactly 30 bars** (29·768 − 768 = 21,504 < 22,272 ≤ 30·768 − 768), and the fixture pins that exact pair. Loops between
the bar cap and the floor therefore have **neither** representation admissible — which the declaration's own comment
already states ("until it lands this declaration admits only a loop long enough to hold the lead below"), and this
slice turns that sentence into the number task 6.3's sweeps and task 3.10's derived envelope need. It is *not* a new
defect: deliberately conservative, and the direction is the safe one.

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **67/67** (was 66; +1 fixture) |
| `pio test -e native -f test_storage_loop_io` | **52/52** (unchanged) |
| `pio test -e native -f test_playback_prewarm` (cited pin) | **190/190** (unchanged) |
| `pio test -e native -f test_midi_service_drain` | **38/38** (unchanged) |
| `pio test -e native` (full) | **2699 cases: 7 skipped, 2692 succeeded, 0 failed** (baseline 2698 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS**, and a **no-op** (2.7 s, no source changed): `variables 96,192` / `code 392,328 + padding 888` / locals 34,880, floor 840, **48 B headroom** |

## Red-proofs

Three mutations, each applied to a **named** file at an anchor whose **occurrence count** was asserted before the run,
each restored **byte-identically** (`shasum -a 256 -c`):

| # | File | Mutation | Observed failure |
|---|---|---|---|
| M65 | `src/Track/TrackPlaybackWindowBuild.cpp` | the admission reserves the whole take even for a demoted loop | `Expected 0 Was 1. the whole-root playback representation is resident exactly while admissible` |
| M66 | `src/Loop/LoopMaterialization.cpp` | the admissibility boundary moves by one bar (`>` → `>=`) | `Expected 1 Was 0. admissibility follows the declared bar cap` |
| M68 | `src/Track/RuntimePlaybackView.cpp` | the window view's storage is no longer the declared capacity | `Expected 2048 Was 2112. the window storage is the declared capacity at every length` |

**Two limits of this fixture, stated rather than glossed.** (1) The admissibility oracle is the *same policy function*
the production request consults (`canCompleteBeforeHandoff`), so a mutation of the contract's declared numbers moves the
fixture's expectation with the code and is **not** detected here — that is deliberate (the fixture pins the
*consistency between the declaration and its consequence*, and 3.10/6.3 own whether the declared numbers are right),
and it is why the floor is *also* derived independently from the contract's fields. (2) The sweep pins the windowed
reservation's **invariance**, not its absolute figure; the declaration-derived absolute figure is already owned by
`test_windowed_only_slot_reserves_no_whole_take_representation` in `test_playback_prewarm`, and is cited rather than
restated.

## Not claimed

- **The parity half of F-8 is not re-proven here.** "Content parity SHALL hold for that demoted state" is 4.3's subject
  (landed: successive windows concatenate row for row to the whole-loop stream) and its spec clause names the
  **content-verification pipeline**, whose windowed profile is 6.4 and whose device legs are 6.5 — owed.
- **The retained-source term is not measured here.** The spec's *Retention is charged* clause belongs to 3.6a's own
  accounting (identity + pins + owned bodies, charged as pool chunks); this slice states the **reservation** half
  (what the view set holds) and does not re-derive that term.
- **No device evidence**, and active windowed playback is still **not admitted** (`ExecutionBounds.md` row 27: the caps
  are declared, the bounds unmeasured).
- **Task 4.4 stays open** on: the parity half with the pipeline (6.4/6.5), the retained-source figure as a recorded
  term, and the sweep's widening (the receipt/timing sweeps are 6.3's).
