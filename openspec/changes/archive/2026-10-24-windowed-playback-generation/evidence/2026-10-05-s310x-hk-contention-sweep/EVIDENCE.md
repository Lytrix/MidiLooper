# Task 3.10 / 6.3 — `H x K` contention sweep: evidence

Packet: [`EXECUTION.md`](EXECUTION.md) (written before the fixture). Slice: **test-only** (measure the declared envelope on
the composed service). Contract: **DEC-108** §13b (`Delta-max x H x bounded per-track work`). Tree: `25542299` plus the
preserved cumulative uncommitted work; no production line was edited by this slice.

## What was measured, and how

The sweep drives the **production composition** -- per track `serviceAdmittedDeadlineMisses` + `playbackWindowServiceOffer`,
`MidiSessionDrain::selectPlaybackHolderRoundRobin` across tracks, `Track::stepPlaybackViewMaintenance` with `nullptr` for the
holder cursor exactly as `src/main.cpp` calls it -- one grain per lane turn, with the playhead advanced by the **declared
permitted advance** (`Track::kPlaybackViewPermittedAdvancePerTurn` = 64 ticks per lane turn). One lane turn is therefore
one opportunity, and the measurement is in the declaration's own unit.

Every shape's **charge** and **service turns** are read from production surfaces (the retained
`PlaybackViewAdmittedWork` record, the per-track budget `playbackViewOutstandingGrains`, the named
`SC_PLAYBACK_BUILD_ROW` rows). Loop lengths are then **sized from the declaration's own arithmetic** over that charge, so
no cell is chosen by hand.

### The cost law (measured, not assumed)

| H (competing tracks) | K (siblings) | trackGrains | drainTurns | target completion | promisedTurns | miss rows |
|---|---|---|---|---|---|---|
| 1 | 0 | 8 | **7** | 7 | 192 | 0 |
| 1 | 1 | 16 | **14** | 7 | 192 | 0 |
| 2 | 1 | 16 | **28** | 15 | 192 | 0 |
| 4 | 1 | 16 | **56** | 29 | 192 | 0 |
| 8 | 0 | 8 | **56** | 49 | 192 | 0 |
| 8 | 1 | 16 | **112** | 49 | 192 | 0 |
| 8 | 2 | 24 | **168** | 49 | 228 | 0 |

Density (H = 8, K = 1) and span (H = 8, K = 1, 20 notes):

| notes | events | charged grains | service turns | trackGrains | drainTurns | loop bars | promisedTurns |
|---|---|---|---|---|---|---|---|
| 5 | 10 | 4 | **2** | 8 | **32** | 17 | 192 |
| 20 | 40 | 8 | **7** | 16 | **112** | 17 | 192 |
| 80 | 160 | 34 | **33** | 68 | **528** | 52 | 576 |
| 20 (long span) | 40 | 8 | 7 | 16 | **112** | 41 | 480 |

1. **The composition's cost law, exact:** `drainTurns = H x (1 + K) x serviceTurns`, and the target (slot 0, served first
   in each cycle) completes at `1 + (serviceTurns - 1) x H`. Every cell in both tables pins it; the `H` ratio (4 → 8) and
   the `K` step (2 → 3 holders) are each measured, not argued.
2. **`K` is load-bearing:** the per-track budget and the siblings' own retained records agree on the charge
   (`othersGrains = K x ownGrains`), and the drain **exceeds** the target's own service turns spread over the same
   rotation -- so charging siblings as *work* is necessary, not conservative.
3. **`H` is load-bearing:** the drain is linear in the competing tracks, so the across-track factor is the rotation
   itself; the declared `(denied + 1) == NUM_TRACKS` has slack below the full rotation and is tight at it.
4. **Density moves the magnitude, not the structure** (2 → 7 → 33 service turns for 5/20/80 notes, the law unchanged), and
   **span is not an input at all** (identical `trackGrains`, `drainTurns`, `targetCompletionTurns` at 17 and 41 bars; only
   the promise grows).
5. **The service turns are *not* the charged grains** -- a measured correction to how the charge reads: the charge carries
   one promotion grain per window (whose work happens inside the completing turn) and the order **bound**, which the
   builder skips when the input is already ordered. For this content the charge is `serviceTurns + 1` (8 vs 7) at 20 notes
   and stays `+1` at 80 notes, while at 5 notes the bound is 2 order grains against 1 actually served. The charge is
   therefore a *sufficient* bound on the turns, never equal to them, which is exactly what the admission needs.

## The boundary, predicted then measured

80 notes, K = 1, H = 8, charge 34 grains per window, **33** service turns, drain **528** lane turns:

- **Inside the charge** (`insideBars = 50`, lead 37,632 ticks = **588** promised turns): every window ready before the
  promise it carries, **0** `one_pass_late` rows, drain 528 (`= 8 x 2 x 33`).
- **Just outside it** (`outsideBars = 27`, lead 19,968 ticks = **312** promised turns; each *individual* request still fits
  its own derived cost, which is what the **unenforced** admission applies): the cell still drains, but its last window
  completes **after** its promised deadline and the crossing is **named** (`one_pass_late`).
- **The declaration's own predicate agrees** with both choices: `canCompleteWithinLead(insideLead, siblingCharge)` true,
  `canCompleteWithinLead(outsideLead, siblingCharge)` false, `canCompleteWithinLead(outsideLead, 0)` true.
- **The control (the oracle can fail):** the same inside cell re-run at **2 x** the permitted advance -- same content, same
  length, same charge -- breaks the promise and names it. The unit the declaration is stated in is the load-bearing one.

Not claimed: this is not continuous two-bar service qualification. The sweep measures the composition's *cost* at the
permitted advance for the swept shapes; steady-state gaps above the permitted advance remain the overrun policy's, long
runs of lost turns remain the lag rule's, and no device leg was run.

## Mutations (each red-proved, then restored byte-identically)

| ID | Mutation (production line) | Failure it produced | Restore |
|---|---|---|---|
| **MC1** | `Track::playbackViewOutstandingGrains` returns 0 — the sibling charge is ignored | all three new fixtures: `Expected 8 Was 0. the per-track budget and the siblings' own records must agree on the charge` (+ the two pre-existing sibling-charge fixtures) | `TrackPlaybackWindowBuild.cpp` `75c23a3f1849d15a` before and after |
| **MC2** | `contract.maximumConsecutiveDeniedTurns = 3` — the placeholder allowance the review corrected | `test_window_contention_sweep_scales_with_density_and_is_span_independent` (`Expected 1037632 to be less than or equal to 1023872. and it stays inside the promise the admission retained`) and the boundary fixture (`… every window of it is ready before the promise it carries`), plus six pre-existing fixtures | same file, same hash before and after |
| **MC3** | `playbackHolderDeadlineIsEarlier`: an equal deadline now wins for the **last** holder (the within-track tie order flips) | `test_window_contention_sweep_measures_the_composed_cost_across_h_and_k`: `Expected 7 Was 14. the target completes on its own service turns' schedule` (+ the pre-existing tie-rotation fixture) | `MidiServiceDrain.h` `9656c7b4b5496811` before and after |
| **MC4** | `kPlaybackViewOrderQuantum` 48 → 96 — the cost model's own unit moves | the grid's measured pin: `Expected 8 Was 5. 20 notes = 40 entries: 1 fill + 6 order grains + the promotion grain` (+ five pre-existing fixtures) | `Track.h` `b9d0ef22549c2c66` before and after |

Restores were byte-identical by `sha256` (the restore is a copy of the pre-mutation file, then re-hashed), and the
production diffstat after the mutation round equals the diffstat before it (`include/Track.h` **43**, 
`src/Track/TrackPlaybackWindowBuild.cpp` **781** changed lines), with `include/Utils/MidiServiceDrain.h` absent from
`git status` entirely.

## Verification (final tree)

| Check | Command | Result |
|---|---|---|
| Focused suite | `pio test -e native -f test_runtime_working_set_ownership` | **135 / 135 passed** (was 133; +3 cases), 5.96 s |
| Full native | `pio test -e native` | **2789 cases / 2782 succeeded / 7 skipped / 0 failed** (was 2786 / 2779 / 7), 84.9 s |
| Capture build | `pio run -e teensy41-capture-serial` | **SUCCESS**, `variables 96,192` / `code 391,576 + padding 1,640` (floor 840, **800 B headroom**), **no re-base**, byte-identical to the pre-slice line |
| Strict OpenSpec | `openspec validate windowed-playback-generation --strict` | valid |
| Whitespace | `git diff --check` | clean |
| Device | — | **NOT RUN** (not claimed; 6.5 owns the device leg) |

Artifacts: `window_contention_sweep.inc` `8b10402590dad557`, the suite TU `26ff165ae5264545`; no production file changed
by this slice (`TrackPlaybackWindowBuild.cpp 75c23a3f1849d15a`, `MidiServiceDrain.h 9656c7b4b5496811`,
`Track.h b9d0ef22549c2c66` are the pre-session hashes).

## Limits and what stays open

- **One test-only seam was added** (a per-(track, slot) request-source buffer and a per-cell world reset) because the
  suite's `windowSourceScratch` is indexed by slot and cannot carry `H` tracks at once. No production state, buffer or
  gate moved.
- The swept **shapes** are bounded: windows published in one bar inside a two-bar interval, 5-80 notes, K ≤ 2, H ≤ 8, and
  the transported-at-permitted-advance clock. Chords, same-tick density, overlapping notes and other tempos are not swept;
  `6.3`'s sweep stays open on those axes and on the device leg.
- **Activation is untouched.** `sInterferenceBudgetEnforced` is still off, `PLAYBACK_WINDOW_ROLLING_ENABLED` still
  defaults 0, and the sweep is not a production requester. The boundary cell shows what the unenforced admission permits
  (a shape whose promise breaks), which is the reason activation is the clock-wiring task rather than a constant.
- Nothing here checks off task **3.10** (its service qualification owns the device enforcement) or **6.3** (the wider
  sweep); this slice supplies the **contention axis** and the envelope's own measurement.
