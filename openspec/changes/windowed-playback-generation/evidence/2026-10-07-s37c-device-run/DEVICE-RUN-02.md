# Second device run of the same leg (repeat): 3.7c reproduces, and the invalidation still has no term

**2026-10-07, on the owner's instruction** (*"upload the window-playback firmware and rerun the hitl"*), same registered
profile `STIM-WINDOW-CONTENT-01@v1`, same track/target shape and the same invocation as the two runs before it.

| item | value |
|---|---|
| image | `teensy41-capture-serial-window-qualification`, `firmware.hex` sha256 `624242aaf050fc74c415ac84ade27021402ed31070f96ee3be6ad49b5e8ed7c5` |
| **tree the image came from** | `339c94bf` **plus 19 uncommitted files** (the in-flight `runtime-working-set-ownership` S7.5b note-edit/playback and persistence slices, which touch `src/Track/TrackPlaybackWindowBuild.cpp` and `include/Track.h`) — chosen deliberately by the owner, so this run is evidence about **that mixed state**, and only the windowed lane's own behaviour below is attributed to `windowed-playback-generation` |
| capture | `captures/s64-37c-02` |
| reference | `captures/s64-37c-01` (same leg, committed 3.7c tree), `captures/s64-demand-01` (before 3.7c) |

## 1. 3.7c reproduces

The active slot this time is index 0 of track 7 (operator slot 1), a **24-bar, span-aligned, demoted, playing** loop
with a current root and **nothing served** — the exact shape of the previous run — and the lane behaves identically:

```
 93.776s x=win_not_servable         sv=0 rt=0 rl=1                 <- before the root is current
103.370s x=win_requested            sv=0 rt=1 rl=1 dm=1 rq=12      <- the first-window request is ACCEPTED
103.467s x=win_waiting              ow=1 dm=1 bld=1                <- build live, coverage source owned
103.528s x=win_blocked_by_request   ow=1 dm=1                      <- invalidated 58 ms later
103.543s x=win_blocked              ow=0 dm=1 rl=1                 <- source retired, demand coalesced back
131.335s x=win_requested            ...                            <- retried, and invalidated again 49 ms later
131.384s x=win_blocked_by_request   ...
```

Where the pre-3.7c run reported `win_not_servable` for that state, both post-3.7c runs report `win_requested`: the
change is reproducible on hardware across two runs and two different slots (index 3 and index 0).

**The lane is also served more, not less:** the gap blocks carry `elig/prog` of `4/3`, `1/1`, `4/3`, `1/1`, **`10/10`**
and `1/1` (six blocks with work), with the readiness receipt `win_readiness,7,4,owes_build,window_build_owner,1,0` —
the demand is admitted, served and still owed when the transport stops.

## 2. The blocker reproduces too — the invalidation has no term

Both `win_blocked_by_request` rows again carry `rq=0`: the row cannot say *which* of the five validation terms
invalidated the retained demand (a changed loop length, a changed clock epoch, a passed admitted position, a passed
deadline, or a source that is no longer current). That is task **3.7d**, recorded as prepared-and-reverted (see
[`../2026-10-07-s37d-invalidation-term/PREPARED.md`](../2026-10-07-s37d-invalidation-term/PREPARED.md)); this run is a
**second independent instance** of the same blocker, on a different slot and a different tree, which strengthens the
case for landing it.

## 3. Honest limits

- **No completion is claimed** on either tree: no `build_complete` publication for the active slot appears in this
  capture, so no window is served from it yet.
- The reused leg's own oracle failed again (`unexpected_loop_len expected≈17664 observed=[13056]`), the same host-side
  expectation mismatch as the previous runs: the record-stop quantization commits a different bar count than
  `--record-bars` asks for (this time 17 bars, which is demoted but *not* span-aligned — yet the slot the lane actually
  served held 24 bars, as the rows above show). Reconciling the leg's `--record-bars` → committed-length relationship
  remains a leg-shape item.
- F-7/F-8/AC-8 and 6.5's four hardware claims stay **NOT RUN**; no timing/service-envelope claim; `ExecutionBounds.md`
  rows 27–29 unchanged; the shipping image still compiles the lane off.
