# DEC-197 — the "full-run only" playback failure is a real 2-bar playback window, not fixture order

Task: the owner named `test_record_note_stream_survives_four_sequential_overdub_publications` as "reproducible in
full runs, green alone", the only signal that the day's playback changes interact with overdub publication order.
It is neither order-dependent nor green everywhere: it is red in the **production-TU oracle** binary and green in the
**fixture** binary, deterministically.

## The two binaries are not the same code

`test/test_playback_prewarm/test_playback_prewarm.cpp` has two compile branches:

| Suite | Branch | Sources | Rolling |
|---|---|---|---|
| `test_playback_prewarm` (252 cases) | `#else` (no macro) | fixture includes, `PlaybackSourceMaskFixture.h` | `static_assert(PLAYBACK_WINDOW_ROLLING_ENABLED == 0)` |
| `test_frozen_record_reader_survival` (9 cases) | `FROZEN_RECORD_READER_SURVIVAL_ORACLE` | production `Track`/`main`/clock via `test_shared_capture_finalization.cpp` | production |

`pio test -e native -f test_frozen_record_reader_survival` fails in **1.9 s**, every time, alone. So the failure is a
production-code fact, not a full-run artifact.

## What the failing run actually serves

Instrumented at the replay assertion (temporary probe, removed):

```
[dec197] captured=6 filtered=4
[dec197] ev 0 tick=12  note=60   ev 1 tick=96  note=60     <- Record bar 0
[dec197] ev 2 tick=244 note=100  ev 3 tick=328 note=100    <- overdub 1
[dec197] ev 4 tick=780 note=61   ev 5 tick=864 note=61     <- Record bar 1
[dec197] winStart=3774 winLen=1536 loopLen=6144 avoidFull=0
[dec197] merged=6 order=6 nextIdx=6 lastTick=6143 builtRev=5 rev=5 builtSnap=…c0 snap=…c0
[dec197] passes rec=1 od=4 currentRoot=1
```

* The loop is 8 bars (6144 ticks); the window is **1536 ticks = 2 bars**.
* `avoidFull=0` — 8 bars is *not* `longLoop` (`shouldAvoidFullVisualRebuild` is true only above 16 bars).
* The Record pass and all four overdub passes are intact (`rec=1 od=4`), and the root is current. **Nothing was lost
  from content**: the served window is what is short.
* `merged=6` is exactly the Record bars 0–1 plus overdub 1 — the events inside one 2-bar slice.

## Cause

`ensurePlaybackMergedMidiEventsBuilt` (`src/Track/TrackPlaybackWindowBuild.cpp`) reused its build on identity alone when
the loop was not `longLoop`:

```cpp
if (!longLoop) {
  if (builtFromRevision == windowRevision && builtFromSnapshotIdentity == buildIdentity) return false;   // reuse
} else if (… && playhead + margin >= winStart && playhead < winEnd) { … }                                // reuse if covered
```

while the window it built was capped for **every** length:

```cpp
const uint32_t winLen = loop.loopLengthTicks < 2 * TICKS_PER_BAR ? loop.loopLengthTicks : 2 * TICKS_PER_BAR;
```

So for any 3..16-bar loop served by the merged fallback, one 2-bar slice was built and never moved: the pass could
only ever sound the two bars it was built around. The function's own comment says the opposite — "a long loop gets the
two-bar window centred on the playhead, **a short one gets its whole loop**" — and the `!longLoop` NOTE_EDIT branch
directly beside it does gather the whole loop.

## Change

The cap is gated on `longLoop`; a short loop is one whole-loop window, as documented:

```cpp
const uint32_t cappedWindow = min(loop.loopLengthTicks, 2 * TICKS_PER_BAR);
const uint32_t winLen = longLoop ? cappedWindow : loop.loopLengthTicks;
```

This is consistent with every other owner of that rule:

* the `!longLoop` NOTE_EDIT branch (whole loop) directly below it;
* `reconcilePlaybackLedgerAfterFullLoopRebuild`, which treats absence from the incoming window as removal **only** for a
  full-loop window — a short loop is meant to be one;
* `isPlaybackMergedMidiEventsReadyForSlot`'s short-loop branch, which does not constrain the length at all;
* `test_rolling_record_retirement`, which already asserts `windowLengthTicks == loopLength` for its short loop.

Cost class is unchanged in steady state: the build is still identity-gated, so a pass with no content change does no
gather. The bigger window lives in `SessionMidiEventVec`, which is `ExternalMemoryFirstAllocator` (PSRAM), not the
internal heap.

## Result

| | before | after |
|---|---|---|
| window (`winStart`/`winLen` for a 6144-tick loop) | 3774 / **1536** | 0 / **6144** |
| merged window events | 6 | 26 |
| Record events served in one pass | **4** | **16** |

Red → green with **no test edit**, and the fixture branch untouched (252/252).

Gates: full native **3607 cases, 2 failed, 7 skipped, 3597 succeeded** — the only failures left are the pre-existing
DEC-193 RED and DEC-194 RED. Focused counts unchanged: prewarm 252/252, shared capture finalization 142/142, continuous
capture 107/107, frozen oracle 9/9. `teensy41-capture-serial` SUCCESS, RAM1 `variables 96,192` / ITCM `code 392,056 +
padding 1,160` (**unchanged**: the edited function is `TRACK_COLD_MEM`, so this costs no ITCM).

## Limits / what is not claimed

* Not device-verified. The change only affects the **merged fallback** — the cold/no-served-source path for 3..16-bar
  loops. If production normally serves those slots from a bridge/window, the device effect may be nil; if it does not,
  this is the difference between two bars and the whole loop. One boot on a set with a 4-bar or 8-bar slot that has
  content settles which.
* The DEC-193 / DEC-194 REDs are untouched and remain the open playback questions.
* No claim about the display path: `shouldAvoidFullVisualRebuild` and the visual-cache gather are unchanged.
