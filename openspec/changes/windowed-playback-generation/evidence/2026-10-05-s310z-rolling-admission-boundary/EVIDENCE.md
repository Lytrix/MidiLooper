# Task 3.10 — the rolling chain's admission boundary on the playing path: evidence

Packet: [`EXECUTION.md`](EXECUTION.md) (written before the fixtures). Slice type: **verification on the playing production
chain** — tests may enable the interference budget; **no production line changed**, no gate flipped, no constant moved
(`git diff --stat -- src include` is empty on the final tree).

Owner's steer for this step: *"Take option 1, but narrow it to the actual rolling path — not another general arithmetic
sweep. Don't enable normal operation yet."* … *"Stop there: record the resulting capability statement and the next
required contract decision. Don't launch another optimization series."*

## The capability statement (workload/lead, not a smaller window)

1. **An admissible window shape on the playing chain repeats its handoffs.** Driving the lane's own composition
   (`rollingTestTick`: selector + one worker turn every 64 ticks, transport every tick) for 16 bars with
   `sInterferenceBudgetEnforced` **on**: `serviceRollingPlaybackWindow` opens the demand at the first aligned interval
   (`startPhase = 6 x 768`, absolute deadline), the worker prepares it, the boundary validates and adopts it, and the
   served view **changes at each handoff** (≥ 2), with **no mutable build at any adoption**
   (`playbackOrderHotRebuildCount` unchanged across every view change), **no promise crossed**
   (`one_pass_late == 0`) and **no demand left behind**.
2. **The emitted trace is exact under enforcement:** **33 events** over 16 bars and 48 ticks — **16 note-ons and 17
   closures of the loop's own note (48), and nothing else** (no silence, no foreign note). The same trace the existing
   rolling fixture pins with enforcement off, now counted from the same boundary with enforcement on.
3. **The boundary is workload against the lead granted, not a smaller supported window.** A **dense** two-bar interval
   (300 notes = 600 entries, prepared cost far beyond the lead the selector grants from the declared basis) is
   **requested but never adopted**: the served view never moves, the episode is prepared and priced (its demand freezes),
   its **obligation keeps its original absolute deadline**, no handoff takes the served view, `one_pass_late == 0`, and
   playback continues through the mutable fallback with the loop's own content (**603** events over the same drive).
   Capacity **2,048**, span **≤ 2 bars** and every timing constant are unchanged; nothing was truncated and nothing was
   made unlaunchable.
4. **What is proven is *intermittent* immutable coverage, not continuous service.** Later two-bar intervals are served
   from immutable prepared copies, with the mutable fallback audible in between; the cost law the `H x K` sweep measured
   still stands (under H8/gap64 the lane supplies 3 turns/window/holder against the measured need), so no contiguous
   two-bar claim is made.
5. **The budget charges work, not only an admitted builder.** With a slot **owning its coverage source** (qualification
   or retirement) and the flag **off**, the per-track budget is **non-zero** while that slot has no admitted work record
   at all; with the flag **on**, a candidate on that track is refused **by name** (`CannotCompleteBeforeHandoff`, its
   obligation retained) and the **very same request is admitted once the source has retired** — so the refusal is
   attributable to the charge and nothing else.

## The next required contract decision (owner)

1. **The frozen/pending asymmetry on the rolling path — should the selector name the refusal?** The selector's refusal
   check reads `runtime->pendingViewRequest()`; `beginCoverageQualification` **discharges** that pending record when it
   freezes the episode (`clearViewPendingRequest()`, documented as *"Freeze discharges ONLY this record"*) and the
   admission writes its `CannotCompleteBeforeHandoff` onto `runtime->frozenViewRequest`. A refusal that happens *after*
   the freeze is therefore invisible to the selector: the episode cannot enter the named `Blocked` state on that path
   (it ends by non-adoption), and the demand — correctly never re-granted — is also never surfaced as refused. **Decide:**
   read the episode's own record (frozen) in the selector's refusal check so the refusal is named and `Blocked`, or keep
   non-adoption as the terminal state for a shape that cannot be prepared inside its lead.
2. **Activation** stays the owner's call and stays **off** in production (`sInterferenceBudgetEnforced` false,
   `PLAYBACK_WINDOW_ROLLING_ENABLED` 0). The previous slice showed the demand that exists carries its stamp and that the
   launch and the capture edge are exempt by design; what remains is the enablement decision plus the device leg (6.5).
3. **The inherited-state defect below must be fixed or worked around** before any test that relies on the S4b.7 helper is
   registered after `test_record_switch_respects_a_retained_stop_and_permits_an_admitted_one`.

## Findings (recorded, not smoothed)

- **F-1 — a named culprit for the earlier "intermittent" launch-test failure (bounded inspection).**
  `test_record_switch_respects_a_retained_stop_and_permits_an_admitted_one` leaves **suite-global** state that makes a
  *later* demoted-slot switch fail to commit. A minimal probe (helper only) bisected by registration position: **passes**
  after RUN_TEST #88 (`test_the_successor_take_commits_with_content_after_a_transport_stop`), **fails** after #89 and
  every later position. Inside the failing helper: the switch is still **pending** while the target's launch **is ready**,
  `holds == 0`, the active slot is unchanged and the pending target index is correct (DIAG `0x12010DC8`:
  `tick=3528, pending=1, active=0, holds=0, ready=1, ownsSource=0, targetSlot=2`). **Eliminated:** re-creating
  `trackManager` + `resetPoolForTests()` + `initPool()` *inside* the probe does not clear it (so the leftover lives
  outside the track/pool layer), and priming the clock tick did not change it. **Not identified:** the mechanism (candidate
  classes: a state-bearing static in the capture-stop/switch path, or a borrowed-buffer lifetime that survives the reset).
  This is the same phenomenon the audit slice recorded as unreproduced — it is now reproducible with a named culprit.
- **F-2 — the frozen/pending asymmetry** described in the decision above (code path quoted there).
- **F-3 — a non-discriminating mutation, recorded for what it says.** Dropping the retry admission's **sibling** charge
  (`playbackViewOutstandingGrains(slotIndex)` → 0) fails **no** test: the dense shape's refusal comes from its **own**
  derived cost, not from a sibling's charge. (So the acceptance item "charge actual qualification/preparation/retirement
  work" is proven by R3's before/after pair, not by the dense shape.)
- **F-4 — the shape of the drive matters and is now pinned.** The selector must be serviced at the lane cadence (every 64
  ticks), not every tick: servicing it every tick changes the episode's targeting and the boundary never behaves as it
  does in production. Both new driving fixtures use the suite's own `rollingTestTick`, so the cadence is the suite's own
  definition rather than a second one.

## Mutations

| ID | Mutation (`playbackViewOutstandingGrains` / retry admission / `Track.h`) | Result | Restore |
|---|---|---|---|
| **MR2** | a slot that owns its coverage source is **not** charged (`else if (false && …)`) | **R3 fails as intended:** `Expected 0 to be greater than 1000. the budget charges the qualification's worst-case snapshot bound` — and no pre-existing fixture fails, so the pin is this slice's | `TrackPlaybackWindowBuild.cpp` `75c23a3f1849d15a` before and after |
| **MR1** | the retry admission's **sibling** charge → 0 | **no test failed** — recorded as **F-3** | same file, same hash |
| **MR1p** | the retry admission ignores the window's **own** derived cost (`!launch && false`) | the focused run did **not** complete inside the tool's budget (other fixtures drive the now-admissible dense shape to exhaustion) — a hang, not a clean red-proof; recorded rather than claimed | same file, same hash |
| **MR3** | `kPlaybackViewFillQuantum` 64 → 128 | eight **pre-existing** cost fixtures fail; the three new fixtures pass — they pin *emission*, not grain cost, and the cost claim is owned by those fixtures | `Track.h` `b9d0ef22549c2c66` before and after |
| **MR5** | `retainRefusedPlaybackViewRequest` returns early (a refusal is not retained) | the focused run **aborted at the first case** (`1 test cases: 0 succeeded`) — a harness-level abort, not a clean assertion failure; recorded | `TrackPlaybackWindowBuild.cpp` restored to `75c23a3f1849d15a` |

Every restore was verified by `sha256` and `git diff --stat -- src include` is empty on the final tree. The honest
summary: **R3 has a targeted, clean red-proof (MR2)**; **R1/R2 are pins** whose discriminating mutations were attempted
and are recorded above, with the reason each attempt did not produce a clean red-proof.

## Verification (final tree)

| Check | Command | Result |
|---|---|---|
| Focused | `pio test -e native -f test_runtime_working_set_ownership` | **140 / 140 passed** (was 137; +3 cases) |
| Full native | `pio test -e native` | **2794 cases / 2787 succeeded / 7 skipped / 0 failed** (was 2791 / 2784 / 7) |
| Capture build | `pio run -e teensy41-capture-serial` | **SUCCESS**, `variables 96,192` / `code 391,576 + padding 1,640` (floor 840, 800 B headroom), no re-base |
| Strict OpenSpec | `openspec validate windowed-playback-generation --strict` | valid |
| Whitespace | `git diff --check` | clean |
| Device | — | **NOT RUN** (nothing is enabled; 6.4/6.5 own the device leg) |

## What stays open

- 6.4 (registered device profile) and 6.5 (controlled device qualification) are the next *steps* the owner named, with
  the fallback and qualification gates retained.
- Task 3.10's service qualification (device enforcement), 6.3's remaining axes (chorus/same-tick density, other tempos)
  and 3.7's fallback removal stay open; nothing here checks a task off. No optimization series was started.
