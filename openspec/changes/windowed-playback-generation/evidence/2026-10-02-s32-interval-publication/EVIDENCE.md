# Slice 3.2 (part) — the owner publishes immutable interval windows, atomically

**Change:** `windowed-playback-generation` · **Task:** 3.2 (publication half) · **Date:** 2026-10-02
**Branch:** `feature/windowed-playback-generation` · **Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md)
**Contract:** DEC-108 items 1 and 3; `ExecutionBounds.md` rows 27–29.

**Status:** the publication half of 3.2 landed and is verified. Task **3.2 stays open** (`[~]` in `tasks.md`):
its enumeration clause (interval-directed, inspected-work-bounded enumeration over the selected `LoopSnapshot`)
and fixture **2.10** follow in the next increment, and the *replacement* sequencing belongs to the successor lane
(3.4/3.5). Nothing is claimed beyond what the fixtures assert.

---

## What landed

| File | Change |
|---|---|
| `include/RuntimePlaybackView.h` + `src/Track/RuntimePlaybackView.cpp` | `RuntimePlaybackViewLifecycle::publishCompletedBuildAsCurrentWindow(views)` — installs a **completed** build as the current interval window. The lifecycle's existing `publishCompletedBuildAsReady` requires a retained/idle successor, which a demoted slot's *first* window can never have (its only representation is windowed), so a windowed slot had no way to publish at all. The invariant that matters is kept and checked: **only a complete view is ever installed** (exactly what `handoff` requires of its ready view); the swap is the same atomic pair `handoff` performs, with the retired window becoming the next build buffer; and a view that `retained` points at is never recycled. |
| `src/Track/TrackPlaybackWindowBuild.cpp` | the owner publishes **inside the turn that completes the build** (`stepPlaybackViewMaintenance`) — publication is atomic, and no consumer read can straddle it; `Track::publishedPlaybackWindowForSlot(...)` reads the published window's identity and coverage (read-only); and `requestPlaybackViewMaintenance` refuses a second window while one is published, with its own named outcome. |
| `include/Track.h` | `PlaybackViewRequestResult::WindowAlreadyPublished`, `publishedPlaybackWindowForSlot(...)`, and the private `publishCompletedPlaybackWindow(...)`. |

**No consumer changed.** Nothing in production reads the published interval window yet: adoption is 3.4 (identity on
the consumers), 3.5 (readiness predicate) and 3.7 (the consumer switch). The live merged-window path, the prepared
pair, the fixed view and the commit binding are untouched, which the *existing* 48 fixtures in this suite and the
full native run confirm.

## The finding that shaped the refusal (measured, not assumed)

The first version of the fixture drove a **replacement**: publish interval A, then request interval B and watch A
stay readable until B's publication. It failed, and the measurement says why:

- with a two-window sequence on one slot, the published window carries the **new** request's source identity
  (generation 4 rather than 3) *before* its build has advanced a single grain — measured via the fixture's own
  assertions (`Expected 3 Was 4`);
- a smaller probe (a four-event source that completes in two grains) did **not** reproduce it.

**Mechanism: UNATTRIBUTED**, and deliberately not papered over. What the measurement *does* establish is a design
fact this slice can act on: **replacement is not a second start.** The lifecycle already owns the correct shape —
publish a *ready* successor and swap it in with `handoffReadyCoverage`, whose atomicity and one-pass-late retention
DEC-108 already fixes — and that sequencing is task 3.4/3.5's. So this slice refuses a second window by name
(`WindowAlreadyPublished`) and asserts that the published window is **unchanged** by the refusal, which is exactly
the immutable-until-adopted property DEC-108 requires. The anomaly as such is recorded here for whoever builds the
successor lane: a `beginCoverageBuild` against a slot whose window is already published must never be the
replacement path.

## Verification (all green, on this tree)

| Command | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **49/49** (the new publication fixture included) |
| `pio test -e native -f test_midi_service_drain` | **38/38** |
| `pio test -e native -f test_playback_prewarm` | **190/190** (unchanged — the suite the change cites, not restates) |
| `pio test -e native` (full) | **2680 cases: 7 skipped, 2673 succeeded, 0 failed** |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192` (**the approved line, unchanged**) / `code 392,136 + padding 1,080` / locals 34,880, floor 840, 240 B of headroom |

The new fixture (`test_window_owner_publishes_the_interval_window_atomically`) asserts:

1. **nothing is published before a request**, and **an incomplete build is never published** — checked after every
   single turn;
2. the publication happens **in the turn that completes the build**, reporting `BuildComplete`;
3. the published window carries the **identity it was built from** (revision, generation, snapshot identity) and
   the **coverage interval** it serves (start, length, loop length);
4. the slot is **demoted** (`fullTakePlaybackViewAdmissible()` false) — so a published window here is a genuine
   windowed publication, not a whole-take oracle view;
5. a second window is **refused by name**, the published window is byte-for-byte unchanged by the refusal, nothing
   becomes owed, and a further owner turn changes nothing (the refusal is not a hidden request).

**Red-proofs (two mutations, each restored byte-identically** — `src/Track/TrackPlaybackWindowBuild.cpp` sha256
`…owner32` recorded in the session): **removing the publication call** fails the fixture at *"the completed build
must be published in that same turn"*; **removing the `WindowAlreadyPublished` guard** fails it at *Expected 3 Was
0* (the refusal returns `Admitted`, and the published window then changes).

## Still owed by 3.2 (named, not claimed)

- **The enumeration clause** — "enumeration goes through the effective-content resolver over the selected
  `LoopSnapshot` … rather than raw committed chunks": the owner currently takes an **already-resolved source**
  (3.1's request shape). The next increment adds the owner-side resolution — the interval policy (whole-loop
  coverage for a short loop; a two-bar window ahead of the playhead for a demoted one), the
  `Loop::tryGatherPublishedSnapshotEventsInWindow` gather over the selected snapshot, and the generation-qualified
  identity — so a caller asks for a *slot and a phase* rather than supplying events.
- **Fixture 2.10** (inspected-work bound): the same requested window must cost the same inspected work on a 4-bar
  and a 64-bar loop, with a window that intersects nothing included.
- **Replacement sequencing** — 3.4/3.5's successor publication + `handoffReadyCoverage`, per the finding above.

## Not claimed

- No device evidence, no flash, no stimulus.
- No capacity, density, `N`, deadline-margin or residency claim.
- Task 3.2 is **not** checked off: the two items above are its own clauses.
