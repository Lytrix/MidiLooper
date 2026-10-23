# Slice 3.2 (part) — the owner publishes immutable interval windows, atomically

**Change:** `windowed-playback-generation` · **Task:** 3.2 (publication half) · **Date:** 2026-10-02
**Branch:** `feature/windowed-playback-generation` · **Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md)
**Contract:** DEC-108 items 1 and 3; `ExecutionBounds.md` rows 27–29.

**Status: 3.2 landed and verified** — publication, the interval policy, the enumeration through the selected
`LoopSnapshot`, the identity it is stamped with, and fixture **2.10**'s inspected-work bound. Two things stay
outside this slice and are named rather than claimed: *replacement* sequencing (the successor lane, 3.4/3.5) and
the consequence of the declared contract's unmeasured phase advance (below).

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

## The enumeration clause and fixture 2.10 (the second increment)

| File | Change |
|---|---|
| `include/Track.h` + `src/Track/TrackPlaybackWindowBuild.cpp` | `Track::playbackWindowInterval(loopLengthTicks, demoted, playheadPhase, out)` — the **interval policy as one pure function**: whole-loop coverage where the whole-take view is admissible, else the two-bar window at the playhead, clamped into the loop (the same shape the live build uses, so the windowed representation is one policy, not two). And `Track::requestPlaybackWindow(slotIndex, playheadPhase, scratch)`: the caller names a **slot and a phase**, never events. |
| same | the resolver enumerates through `Loop::tryGatherPublishedSnapshotEventsInWindow` — the selected `LoopSnapshot`'s window membership, never raw committed chunks — and refuses by name when there is no published root (`SourceUnavailable`) or when the root is **not current** (`SourceNotCurrent`, i.e. the gather would have to overlay unpublished session/pending passes); only the `Snapshot` branch is served. It stamps `{loop.playbackRevision, playbackGeneration, loop.publishedSnapshotIdentity()}` and hands off at the interval's own start ("readiness is owed per affected tick, before the playhead returns to it"). |
| `include/Loop.h` + `src/Loop/LoopSnapshotPlayback.cpp` | the gather reports **inspected work**: an optional out-parameter counting every snapshot membership reference the walk *looked at* — including the ones the window rejects — for the window's bars. It is the number fixture 2.10 bounds, and it costs nothing when unused (all nine other call sites keep the default). |

**Fixture** `test_window_owner_resolves_the_interval_through_the_published_snapshot` asserts:

1. **the policy**: a 4-bar loop → whole-loop coverage `{0, loopLength, loopLength}`; a demoted loop at playhead
   5 000 → `{4 232, 1 536, 49 152}`; playhead 100 → start 0; playhead near the end → clamped to
   `loopLength − 1 536`; a zero length refuses;
2. **the resolver**: `Admitted` on a demoted slot with a published root; the gathered content comes from that
   root; after the owner's grains complete, the **published window's interval is exactly the policy's**, its
   revision is the loop's current `playbackRevision` and its `snapshotIdentity` is the published root's — the
   identity clause of 3.4 is therefore already carried, though no consumer reads it yet;
3. **a rootless slot is refused by name** (`SourceUnavailable`) rather than served through a compatibility path;
4. **fixture 2.10**: the same requested window costs **identical inspected work** (4 references) and gathers the
   same events on a 4-bar loop and on a 64-bar loop whose unrelated content is 32× larger; and a window whose
   bars carry no membership at all inspects **0** and gathers **0** — the walk skips a missing bar instead of
   scanning the loop.

**Red-proofs for this increment (two mutations, each restored byte-identically** — `TrackPlaybackWindowBuild.cpp`
and `LoopSnapshotPlayback.cpp` sha256 recorded in the session): the policy made to ignore demotion fails at
*Expected 4232 Was 0*; the inspected-work counter removed fails at *"the short loop's window has content, so the
walk is non-empty"*.

Verification for both increments together: this suite **50/50**, `test_midi_service_drain` 38/38,
`test_playback_prewarm` 190/190, full native **2681 / 2674 succeeded / 7 skipped / 0 failed**,
`teensy41-capture-serial` **SUCCESS** with `variables 96,192` unchanged.

## Named consequences and what is deliberately not this slice's

- **A whole-loop interval is admitted only when the declared lead fits.** The declaration's per-turn phase
  advance is still unmeasured (conservative: one bar), so `canCompleteBeforeHandoff` refuses an ordinary loop's
  whole-loop window (a short loop's available lead is its own length). That is the *safe* direction of an
  unmeasured term and is the measurement task 6.3 owes; the windowed policy itself is asserted without it.
- **Replacement** — the successor publication plus `handoffReadyCoverage` — is 3.4/3.5's, per the measured
  finding above.

## Not claimed

- No device evidence, no flash, no stimulus.
- No capacity, density, `N`, deadline-margin or residency claim.
- Task 3.2 is **not** checked off: the two items above are its own clauses.
