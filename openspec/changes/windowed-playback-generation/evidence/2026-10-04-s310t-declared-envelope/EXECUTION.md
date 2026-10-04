# Task 3.10 (increment 25) — review step 4: the declared service envelope

Baseline: `b8e95b38` (increment 24 landed). Branch `feature/windowed-playback-generation`.
Work order: the reviewer's own plan, recorded in `G2-EXECUTION-PACKET.md` ("step 3's instrumentation + device leg, then
step 4's formula and enforcement"; "Step 4's `Delta-max x H x bounded per-track work` turns it on"). Authority:
DEC-108 §13(a)-(e) and the spec's *One off-clock owner … declares and honours a bounded service contract* requirement.

## What step 4 was, and what this increment did with it

The formula's three terms were placeholders:

| Term | Before | Now | Where it comes from |
|---|---|---|---|
| `Delta-max` (per opportunity) | `TICKS_PER_BAR` = **768** (one bar per turn) | **16 ticks** | the schema-2 lane measurement: steady state **2.9-5.5 ticks** per opportunity, **p50 8**, tempo-invariant rotation **7.3-7.5 ms** over the declared 20-300 BPM basis. 16 = **2x p50** and **~2.3x the measured rotation at the fastest declared tempo** (16.7 ms at 300 BPM; 41.7 ms at 120 BPM; 250 ms at 20 BPM) |
| `H` (across-track rotation) | `3` (an allowance, not the rotation) | `Config::NUM_TRACKS - 1` = **7** | every eligible track is served within H lane turns (`src/main.cpp`'s offer round-robin) |
| bounded per-track work | the **declared basis counts** (3/3/1 = 7 grains) judged the lead | the window's **derived** cost (`deriveGrainsFrom`) | increment 18's single source of truth, now used by the lead judgement too |
| overrun policy | unstated | the **lag rule** (DEC-108 §13a/§13e, task 3.6b): one-pass-late is served and reported, the second consecutive miss latches `deadlineContractFaultLatched`, the deadline is never silently re-granted | named in the contract's declaration, where the terms live |

`Delta-max` is a **declared permitted value, not a measured WCET**: it bounds the *scheduling* advance, and the
seconds-scale starvation episodes (2 138 / 4 355 / 8 520 ticks) are *lost opportunities*, which no per-turn constant can
hold — the conflation increment 17 corrected. The margin is deliberately tight (2x p50): this figure multiplies the
whole per-track work, and a loose factor is exactly what the placeholder got wrong.

## Activation: attempted, measured, and **not** flipped — the finding this step produced

The plan was to flip enforcement on as part of step 4. Measured, that is **not a supported production state yet**:
enforced admission fails closed on an **unqualified clock stamp** (increment 22's own rule, DEC-108 §13c), so with the
switch on **twelve fixtures that prepare a window without a stamp are refused by name**
(`TrackInterferenceBudgetExceeded`) — correct behaviour for an unqualified demand, and impossible for the calling owners
today, because the MIDI edge, the switch preparation and the preview lane **do not pass stamps yet** (no production
caller requests a window at all). So activation needs the **clock-owner wiring**, not a better constant. The switch
therefore stays off, the enforced path stays exercised by the fixtures that turn it on, and activation is recorded as
the step-4 remainder with its cause. Nothing in production changes either way.

## Recorded re-bases (every one deliberate, with its reason)

The declaration change reaches fixtures, and each move is recorded rather than smoothed. **No fixture was re-based to
hide a capacity change:** the declaration *is* the capacity statement, and where a shape no longer fits it, the fix was
to move the shape **inside** the declared envelope, or to re-point a pin at the new declared value.

1. `kWindowOwnerExpectedAdvancePerTurn` 768 -> 16 and `kWindowOwnerExpectedDeniedTurns` 3 -> `NUM_TRACKS - 1`: these
   constants *mirror the declaration*, which is the declares-caps fixture's purpose.
2. `test_window_owner_declares_its_caps_and_the_handoff_margin_is_the_oracle`, the one-grain, the atomic-publication and
   the grain-cost fixtures placed their requests with the declared-basis lead; they now compute
   `declaredRequiredLeadFor(count)`, the contract's own arithmetic on the **derived** cost of their own source
   (`deriveGrainsFrom`), because an expectation built from the basis counts describes a request the firmware no longer
   makes. *(A first attempt at this left a fixture asserting `kWindowOwnerExpectedRequiredLead - 1` and admitted it:
   recorded, not hidden.)*
3. `test_window_owner_service_gap_declaration_is_bounded_by_measurement_not_by_its_own_claim`: the two wall-clock
   absolutes moved with the declared value (2 000/1 000 ms -> **41/20 ms**), the relation they carry unchanged.
4. The request stamps in the shared window helpers are now **monotone device time** (`nextWindowRequestClock`), because
   the interference budget refuses a clock that goes *backwards* and the urgency fixtures deliberately request a
   farther interval first and a nearer one second. Consequences, each recorded: the deadline-clock fixture reads its
   reference tick from the admitted record instead of reconstructing it from `phase`; the budget fixture's
   phase-equals-stamp assertion became "the record is qualified and its deadline is that stamp plus the interval's
   lead"; its lap assertions count laps from the admitted deadline.
5. Shapes that the *charged* envelope can no longer host moved inside it: the urgency / readiness / request-shape
   fixtures 40 -> **200 bars** (they prepare two holders on one track); the increment-24 retention fixtures 40 -> **30
   bars** (they exist to be refused, and 30 bars admits the first holder while the charged sibling envelope refuses);
   the charge fixture's slot 0 100 -> **4 bars** and slot 1 200 -> **20 bars** (so the H-factor, not the loop, is what
   decides, which is that fixture's point); the refusals-by-name fixture 900 -> **30 bars** (on a 900-bar loop nothing
   can break a promise under the declared envelope).
6. The demoted-residency sweep's floor assertion became a **monotone straddle**: with a derived per-row cost the floor
   is derived per row too, so the fixture pins that the answer flips once (a longer loop never stops being preparable)
   instead of pinning one number.
7. The grain-cost fixture's structural pins now assert the declared permitted bound instead of "the placeholder bar",
   and its "incomplete until the tracks x slots contention is declared" note is replaced by the same-track-sibling
   rule the declaration states.

## Verification

- Focused `test_runtime_working_set_ownership`: **90/90** (was 89; +1 envelope fixture), including the new
  `test_the_declared_envelope_states_its_terms_its_derivation_and_what_it_refuses` (three pins: the terms and their
  derivation, the derived-versus-basis accounting, and the capability — the same 63-entry window charged with a sibling
  fits 40 bars, not 4, and the placeholder advance refused it).
- `pio test -e native`: **2,733 cases / 2,726 succeeded / 7 skipped / 0 failed**.
- `pio run -e teensy41-capture-serial`: **SUCCESS**; RAM1 **`variables 96,192` unchanged** (no RAM1 re-base, no new
  DTCM state, no size-pin movement — this increment changes constants, the lead-side accounting and comments).
- `git diff --check` clean; `openspec validate windowed-playback-generation --strict` valid.
- **Mutations (guarded replace, byte-identical restores).** Restore hashes: `include/Track.h`
  **5c4a5b0121796179**, `src/Track/TrackPlaybackWindowBuild.cpp` **f653a2d6559ac744**.
  - **M127** the per-turn term back to the placeholder bar (16 -> 768): the envelope fixture fails on
    *"and it is the stated margin over the measured per-opportunity advance"* (`Expected 16 Was 768`), and **23 fixtures
    in total** refuse — the declaration is load-bearing in both directions.
  - **M128** the lead judged on the declared basis counts (the `contract = derived;` line removed): the charge fixture
    (*"candidate must charge derived work across all eight tracks"*) and the declares-caps boundary fixture fail.
  - **M129** the denial allowance back to `3`: the envelope fixture fails on
    *"the denial allowance is the across-track rotation (H - 1), not an invented 3"* (`Expected 7 Was 3`), and the
    H-factor fixture fails.
- Device: **NOT RUN** — no upload, no HITL, no timing-envelope claim. The image is unchanged in RAM shape and no
  production path requests a window, so there is nothing new to observe on the card.

## Still owed (task 3.10 stays open; its checkbox is untouched)

1. **Activation** = the calling owners' **qualified clock-stamp wiring** (the MIDI edge, slot preparation, preview), the
   blocker this step found; then flipping `sInterferenceBudgetEnforced`.
2. The re-attempt that serves a retained demand (**increment 24's O8**).
3. A **production requester** of committed windows (the qualification's blocker 1).
4. The **permitted** service-gap policy as an owner ruling: this increment declares Δmax=16 with its derivation, its
   bounds and its overrun mapping — the owner can confirm it or tighten it, which is now a one-constant change plus the
   fixture pins that mirror it (M127 proves the change is detectable).
5. The `H x K` *sweep* and the device leg for the scheduling bound.
