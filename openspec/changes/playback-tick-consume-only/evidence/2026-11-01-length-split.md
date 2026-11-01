# Why loop length still picks the playback route (investigation, 2026-11-01)

The owner asked why the restored 1-bar and 8-bar loops (`2026-11-01-route-reject.md`) use a different route from the
20-bar loop, when the goal is one way of serving every loop.

## The direction is already recorded and accepted

- **DEC-200 (2026-10-29), owner:** "length should not be the discriminator, there should only be one DRY logic to
  handle playback. short and whole loop logic should be removed everywhere in the codebase."
- DEC-200 unified the readiness predicate. It named the **keystone** that still splits the lane by length and held it
  back until DEC-194's rootless first window worked.
- **DEC-201 / DEC-202 (same day):** the DEC-194 RED was three fixture preconditions, and the late-completion deadlock
  was fixed. DEC-202 concludes: "DEC-200's keystone is now unblocked … can be flipped so every loop takes the window
  lane, after which the interval slice … lands and the legacy merged/bridge fallbacks are deleted."
- **The flip never landed.** DEC-203 says it is "still the next slice". No later decision (DEC-204 … DEC-241) touches
  it.

## What the code does today: three length-specific readers over one fallback

| Loop length | Intended reader | Who publishes it | When |
|---|---|---|---|
| exactly 1 bar | the cold whole-loop rolling frame (`coldReady` / `coldWaiting` in `resolveRollingPlaybackWindow`, `coldRollingShape` / `coldLayeredSlotFrameForEmission`) | prepared while stopped (epoch 0) | before transport starts |
| 2–16 bars | a published **full-loop view** or bridge (`resolveFixedPlaybackSourceForTurn`) | the **stopped oracle** in `ensurePlaybackMergedEventsForSlot` | only while `!anyTimingCriticalTrackActive()`, i.e. every track stopped |
| > 16 bars | rolling two-bar windows (`serviceRollingPlaybackWindow` → `requestCommittedPlaybackWindowForSlot`) | the windowed lane, off-clock | continuously while playing |
| any | the merged fallback | built on demand (main loop), consumed by the ISR (DEC-241) | whenever the reader above is absent |

The two gates that keep 1–16-bar loops out of the windowed lane:

- `Track::requestCommittedPlaybackWindowForSlot`: `if (!demoted) return NotServable`, where
  `demoted = shouldAvoidFullVisualRebuild(len)`, i.e. `len > 16 bars`. This is DEC-200's keystone.
- `Track::serviceRollingPlaybackWindow`: `!loop.shouldAvoidFullVisualRebuild(len)` → `win_window_ineligible`. This is
  the producer-side twin, not listed in DEC-200's census.

**Census today.** `shouldAvoidFullVisualRebuild` / `fullTakePlaybackViewAdmissible` appear 21× in
`TrackPlaybackWindowBuild.cpp` and 9× elsewhere in the track and transport code. There are 10 exact-one-bar
(`== TICKS_PER_BAR`) loop-length tests in `TrackPlaybackWindowBuild.cpp`.

## Why t0 (1 bar) and t1 (8 bars) were merged from their first tick

Neither short-loop reader can be produced once the transport is running:

- The 2–16-bar full-loop view is published **only while every track is stopped**. Its source identity
  (revision + generation + snapshot) must still match exactly when playback starts. Nothing republishes it while
  playing.
- The 1-bar cold frame is likewise prepared only while stopped.

In this run the rig starts the transport at 14.07 s, while the boot restore is still settling. Either the stopped
publication never ran for these slots, or its identity changed before the first tick. **This capture cannot say
which**: the default image has no row for the stopped oracle's publish or refusal, and the qualification-only `win_*`
rows are compiled out.

Either way, the outcome follows from the design: a short loop that misses the stopped publication **is merged for the
rest of the session**. The windowed lane, the only producer that runs while playing, refuses it by length.

## Bearing on this change

- **The long-loop fault and the short-loop fault are the same gap: a missed or stale reader has no in-play recovery.**
  - Long loop: an epoch change permanently blocks the frame.
  - Short loop: the lane refuses it by length, and the only publisher is the stopped oracle.
- Flipping the keystone (and its producer twin) puts every length on one producer that runs while playing. This is the
  owner's DEC-200 direction. After the flip, `NoFullLoopView` would no longer be a predicate to name: the short loops'
  reason would come from the same resolver as the long loop's.
- **Recommendation:**
  - Phase 3's capability fixture should sweep **1, 8 and 20 bars** through the same lane, not 20 alone.
  - Phase 4's repair should be designed as **DEC-200's keystone flip plus epoch-change recovery**, followed by the
    interval slice (`playbackWindowInterval`'s `demoted` argument and its two fixtures) and the deletion of the
    length-specific readers.
  - DEC-193 still applies to phase 4's design note (window-first material is parked). DEC-200 / DEC-202 are the owner's
    own later direction for exactly this lane, and the note must reconcile the two.
