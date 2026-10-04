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

---

## Addendum (same session): the device leg, and the value set from it — `Delta-max` 16 -> **64**

The owner's ruling on the value was **"prove it on the device first, then set the value"**, so the declared `Delta-max`
above was treated as provisional and measured. It was **falsified**, and the declaration was raised. Nothing above is
rewritten; this section is the correction.

### What was measured, and on which image
Two managed runs of the **authoritative entry point** (`scripts/host_midi_hitl.py run --preset base --track 8
--loop-slot 4 --midi-channel 8`), canonical stimulus `STIM-BASE-2-2-01@v1` unchanged, no stimulus variant, no captured
`command.json`:

| Leg | Clock | Capture | Rows | Opportunities | Verdict |
|---|---|---|---:|---:|---|
| A | internal, default tempo | `captures/step4-lane-internal-led` | 452 | 68 352 | FAIL (`FIRMWARE_CONTRADICTION`: `persistence_pers_work_slice_too_large`, `persistence_persist_save_slice_too_large` — the large-loop bounds verifier's 100 ms slice declaration, max 104 640 us) |
| B | host clock, **300 BPM** (`--boot-reset`) | `captures/step4-lane-external300` | 194 | 5 888 | **PASS** |

**Image honesty, stated plainly:** both legs ran the **flashed** image `7c2a1377…`, i.e. the tree at `4a7cef70`
(increments 22-23) — *not* this increment's tree. That is deliberate and it is checkable: increments 24-25 do not touch
the measured path — `git diff 4a7cef70..HEAD` shows no change to `src/main.cpp` (the lane dispatcher),
`include/Utils/PlaybackLaneMeasurement.h` (the sampler) or `Utils/DebugSessionCapture.*` (the row emitter); increment 24
added a *function* to `TrackPlaybackRuntime.cpp` and increment 25 changed the contract constant and the admission's
accounting, and **no production path requests a window**, so the admission is never called in this flow and the grain
steppers are untouched. A run on the increment-25 image is **not claimed**, and no upload was performed (the upload rule
was not exercised). The content verdicts above are recorded as they came out; the lane measurement does not depend on
them.

### The quantity, now separated rather than conflated
The per-turn term bounds the advance across a turn **whose predecessor was eligible** — work was owed and the lane's
next turn was delayed. `DIAG,win_gap`'s `eligiblePredecessorMax` is that figure; the *all-dispatch* maximum is not (it
includes idle intervals, the conflation increment 23 withdrew).

| Leg | `eligiblePredecessorMax` p50 / p90 / p99 / **max** | all-dispatch p99 / max | resets | saturated gaps | eligible / progress / denied |
|---|---|---|---|---:|---:|---|
| A internal | 0 / 9 / 15 / **17** | 131 / 366 (max with an **ineligible** predecessor = idle) | 6 (excluded, reported) | 0 | 388 / 386 / 2 |
| B 300 BPM | 8 / 16 / 48 / **48** | 192 / 192 | 12 (excluded, reported) | 0 | 478 / 478 / 0 |

* **The provisional 16 was falsified by this measurement**: two internal rows and six 300-BPM rows exceeded it.
* The 300-BPM maximum is ~2.8x the internal one, as a tick-denominated unit implies (300/120 = 2.5), and 48 ticks is
  50 ms of wall-clock at that tempo.
* The seconds-scale **starvation** episodes (2 138 / 4 355 / 8 520 ticks) did **not** appear in either leg: 0 saturated
  gaps, and the largest all-dispatch gap (366) had an ineligible predecessor, i.e. it was idle time. They remain the lag
  rule's, not this term's — a bound on one turn cannot hold a run of *lost* turns.

### The declaration now
`Track::kPlaybackViewPermittedAdvancePerTurn` = **64 ticks**: at or above **both** measured scheduling maxima with a
stated margin (1.33x the 300-BPM maximum, 3.8x the internal one), and 66.7 ms at the fastest declared tempo. Capability
consequence, stated: one 63-entry window charged with one same-track sibling needs **11 008 ticks ≈ 14.3 bars** under
this declaration (it was 3 328 ≈ 4.3 bars at 16, and 123 648 ≈ 161 bars under the placeholder), so the declared envelope
supports that shape on a ~15-bar loop and larger windows on proportionally longer loops (measured: a 160-note/320-event
window needs ~90 bars on its own).

### Fixture re-bases this correction required (recorded, not smoothed)
The mirroring constant `kWindowOwnerExpectedAdvancePerTurn` 16 -> **64**; the service-gap fixture's wall-clock absolutes
41/20 ms -> **166/83 ms**; the envelope fixture's margin pin replaced by **measured** pins (it now requires the declared
value to be >= 48 and within 4/3 of it, and >= the internal 17 — the pin that caught the provisional value); the
`kWindowOwnerExpectedRequiredLead`-as-handoff-phase sites now compute `declaredRequiredLeadFor(<their count>)`; the
grain-cost sweep's loops 64 -> **128 bars** (and its loop-length-independence pair 64/32 -> 128/48), because a 160-note
window needs ~90 bars of lead; the two-holder fixtures 200 -> **300 bars**; the retention fixtures 30 -> **70 bars** with
100-note windows; the charge fixture 4/20 -> **12/40 bars**; the refusals-by-name fixture 30 -> **70 bars** with 100-note
windows. Every move is one of the same two kinds as before: a pin re-pointed at the measured declaration, or a shape
moved **inside** it.

### Verification at the measured value
- Focused `test_runtime_working_set_ownership`: **90/90**, zero failures.
- `pio test -e native`: **2,733 cases / 2,726 succeeded / 7 skipped / 0 failed**.
- `pio run -e teensy41-capture-serial`: **SUCCESS**; RAM1 line **byte-identical** (`variables 96,192`, code 390,472 +
  padding 2,744) — constants, accounting and comments only.
- `git diff --check` clean; `openspec validate --strict` valid.
- **Mutations, re-run at the measured value:** **M127** (per-turn term back to the placeholder bar) -> **19 fixtures**
  refuse, including the shape fixtures; **M128** (lead judged on the basis counts) -> the charge fixture
  (*"candidate must charge derived work across all eight tracks"*) and the declares-caps boundary fixture; **M129**
  (denial allowance back to 3) -> the envelope fixture (*"Expected 7 Was 3. the denial allowance is the across-track
  rotation (H - 1), not an invented 3"*) and the H-factor fixture. All restored byte-identically: `include/Track.h`
  **3ae6b899699f2692**, `src/Track/TrackPlaybackWindowBuild.cpp` **1ff579cb96e31498**,
  `src/Track/TrackPlaybackRuntime.cpp` **35eec9683a0fdfd2**, `include/TrackPlaybackRuntime.h` **2e50b234a0cce56c**.
- One device run per tempo, an instrumented build, per-block maxima rather than per-dispatch samples: the declaration is
  set from the worst *measured* scheduling maximum with a stated margin, and the next device leg can tighten it the same
  way. **No upload; no timing-envelope acceptance claim.**

---

## Addendum 2 (same session): the device leg repeated on **this increment's image** (the owner authorized the upload)

The owner authorized flashing, so both tempos were measured again on the image that *declares* the bound, not on the
previously flashed one. Three legs, `--build-upload` building and flashing `16a1f931` (hex
**`bde422e5659a7b7c34655b475325462e37e51c63e455af01b112f5bca3249967`**, `metadata.json` records
`git_commit 16a1f931…`, **`git_dirty: false`**):

| Leg | Image | Clock | Capture | Rows / opportunities | Verdict | `eligiblePred` p50 / p90 / p95 / p99 / **max** | >64 |
|---|---|---|---|---:|---|---|---:|
| A | current `bde422e5` | internal | `captures/step4b-lane-internal-current` | 94 / 4 864 | INCONCLUSIVE_CAPTURE (EVIDENCE_MISSING; `persistence_active_snapshot_unavailable_during_save`, `display_empty_after_overdub_stop`) | 7 / 8 / 17 / **70** / **70** | 1 |
| A-retry | current | internal, fresh boot (`--boot-reset`) | `captures/step4b-lane-internal-current-retry` | 87 / 6 144 | **PASS** | 9 / 10 / 10 / 22 / **22** | 0 |
| C | current | host clock, **300 BPM** (`--boot-reset`) | `captures/step4b-lane-external300-current` | 94 / 5 888 | **PASS** | 8 / 8 / 16 / 48 / **48** | 0 |
| A' | flashed `7c2a1377` | internal | `captures/step4-lane-internal-led` | 226 / 68 352 | FAIL (large-loop slice bounds) | 0 / 9 / 9 / 15 / **17** | 0 |
| B' | flashed | 300 BPM | `captures/step4-lane-external300` | 97 / 5 888 | **PASS** | 8 / 16 / 16 / 48 / **48** | 0 |

**What reproduces, and what does not.** The **fastest declared tempo gives 48 ticks on both images, to the tick**
(two independent runs), and the internal *scheduling* population is stable at p90 8-10 / p95 17 across the four internal
legs. No leg reported a saturated gap. The one variable figure is the internal **tail**.

**The single exceedance, classified with evidence rather than excused.** The only row above 64 in five legs (1 of 411
rows with work, 0.24%) is `#CAP,27539099,DIAG,win_gap,75,1536,2849,1,1,70,4,4,0,295,4,0,2` — and its *neighbourhood*
in the same capture shows what held the loop: `DIAG,snapshot_unit,kind=step` immediately before it and
`kind=step` / `kind=alloc,nodes=4561,bytes=327240` / `kind=finish` / `kind=publish` immediately after, i.e. a
**4 561-node snapshot allocation and publication** was in flight across that gap, while the lane's own worker took
**295 us**. That is a **blocked** gap — a heavy non-lane owner holding the loop — not a scheduling delay of this
owner, which is exactly the class the declaration does not claim to bound and the overrun policy covers. Enlarging the
constant to cover it would be the repair increment 17 forbade (its fixture pins `declared < the measured starvation
episode`), and it would trade away real capability for a figure that a *run* of lost turns can exceed anyway.

**The declaration therefore stands at 64 ticks, with its basis stated precisely:** at or above the **measured maximum at
the fastest declared tempo (48, reproduced on two images)** with a 1.33x stated margin, and at or above the internal
scheduling population (p90 8-10, p95 17, 22 in the retry) with >=2.9x. It is **not** claimed as a bound on every gap an
observer will see: a blocked owner can exceed it, and that miss is reported and contained by the lag rule rather than
absorbed into the constant.

**The leg-A verdicts, recorded as they came out.** `INCONCLUSIVE_CAPTURE (EVIDENCE_MISSING)` with
`persistence_active_snapshot_unavailable_during_save` and `display_empty_after_overdub_stop` — and the **retry on the
same image PASSED**, so the display issue did not reproduce and is a run/capture artifact of that leg, not a finding
about this increment. That reading is backed rather than assumed: increments 24-25 change **no production path** — the
ITCM code figure is byte-identical (`code 390,472`, so the pinned layout is unchanged), the added code is `FLASHMEM`
(not ITCM), `src/main.cpp`, the sampler and the capture emitter are untouched, and the admission is never called because
no production path requests a window. The INCONCLUSIVE leg's lane rows are nonetheless included above as evidence of the
distribution (they are the reason the tail is visible at all).

**One run per tempo per image, an instrumented build, per-block maxima** — the declaration is set from the worst
*measured scheduling* maximum with a stated margin; the next leg can tighten it the same way, and nothing here is an
acceptance claim for the timing envelope as a whole (activation, the re-attempt and a production requester are still
owed).
