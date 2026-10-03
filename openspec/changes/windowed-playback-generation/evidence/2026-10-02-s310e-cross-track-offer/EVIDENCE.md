# Slice 3.10 (increment 5) — the cross-track offer and selection

**Change:** `windowed-playback-generation` · **Task:** 3.10 (**stays open** on the starvation half, the envelope
derivation and the grain-uniformity item; the cross-track question increment 4 raised is now **measured**) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Increment 3.10 (increment 5)
**Answers:** the question increment 4 recorded — "it exercises holders *within* a track, while the same starvation
argument applies to the `main.cpp` loop that stops at the first *track* with work".

---

## What landed

| File | Change |
|---|---|
| `include/Track.h` | `Track::playbackWindowServiceOffer(uint32_t currentPhase) const` — this track's **most urgent owing holder**, read without serving — and `fillPlaybackHolders(...)`, the one description of a holder the offer and the turn both read. |
| `src/Track/TrackPlaybackWindowBuild.cpp` | The holder-set build is extracted from `stepPlaybackViewMaintenance` into `fillPlaybackHolders`; the offer resolves the most urgent eligible holder; the turn calls the same filler and the shared policy. |
| `src/main.cpp` | The turn now builds **every track's offer** and lets `selectPlaybackHolder` choose **one track**, which then serves its own most urgent holder — instead of looping tracks and stopping at the first with work. |

**Why the level matters.** Increment 4's fixture showed the *slot*-level rotation working, but on the production path a
track almost always owes at most one holder, so the slot level adds nothing there — while the **track** level is where
the starvation argument bites: the loop this replaces served the lowest-index track with work *every* turn, so a track
whose interval was a whole pass away monopolised the opportunity while a later track's interval fell late. The offer
read is what makes the choice possible without serving, and the tick lead is what makes the tracks comparable (the
reason increment 2 existed).

**RAM1:** the offer array lives on the caller's frame and the rotation is the existing `DMAMEM` byte, so the approved
line is untouched — the build guard confirms it (below).

## The fixture

`test_window_opportunity_is_offered_across_tracks_by_urgency_not_by_index`, on **two real tracks**:

| Block | Assertion |
|---|---|
| **A** | each track answers for itself, read-only: both offers eligible with a deadline, the near track's lead the smaller, and (the read-only claim) **both tracks still owe** after reading |
| **B** | with every track's offer in one array, the policy chooses the **urgent track — which is the higher index**, exactly what the ascending loop could not do |
| **C** | equally urgent tracks **rotate**: two selections with one cursor choose different tracks |
| **D** | the choice is executable: serving the chosen track advances *that* track's holder and leaves the far one |
| **E** | the offer's own aggregation: a track with **two** owing holders offers its most urgent, not its lowest slot |

**Block E exists because a red-proof found the gap.** M79 (the offer returning the first owing holder) **passed** the
fixture as it stood, because every track had a single owing holder — so the per-track "most urgent" was never
exercised. The block was added, and M79 now fails. That is recorded rather than quietly fixed.

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_runtime_working_set_ownership` | **69/69** (was 68; +1 fixture) |
| `pio test -e native -f test_midi_service_drain` | **39/39** |
| `pio test -e native -f test_storage_loop_io` | **52/52** |
| `pio test -e native -f test_playback_prewarm` (cited pin) | **190/190** |
| `pio test -e native` (full) | **2702 cases: 7 skipped, 2695 succeeded, 0 failed** (baseline 2701 → +1) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888`, 48 B headroom |

## Red-proofs

| # | Mutation | Observed failure |
|---|---|---|
| M78 | the offer's urgency is ignored (`handoffLeadTicks = 0`) | the new fixture (`the near track's interval is the imminent one`) **and** the increment-4 holder fixture |
| M79 | the track offer returns the **first** owing holder instead of the most urgent | **passed before block E existed**; after it, `Expected 32 Was 10032. the track offers its most urgent holder, not its lowest slot` |

Both applied to the named file at an anchor whose occurrence count was asserted, and both restored **byte-identically**
(`shasum -a 256 -c`).

## Not claimed

- **The caller's own shape is build-verified, not fixture-proved.** `src/main.cpp` is not native-linkable (the
  `build_src_filter` this change's packet already records), so the *selection* is proved natively over **real
  tracks' offers** and the caller's construction of that array is proved by the build. That is the same boundary the
  owner-turn placement has carried since slice 3.1, restated here rather than newly introduced.
- **Still not reachable in production:** nothing requests windows yet, so no live behaviour changed.
- The **starvation half** (capture finalization / persistence keeping their own opportunities), the **envelope
  derivation** from grain costs and the worst permitted service gap, and **"one declared grain per turn"** as a
  uniform invariant remain open — unchanged by this increment.
