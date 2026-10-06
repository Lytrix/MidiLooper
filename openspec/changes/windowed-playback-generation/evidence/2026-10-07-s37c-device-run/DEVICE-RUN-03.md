# Third device run: clean rebuild + rerun on the current (uncommitted) tree

**2026-10-07, on the owner's instruction** (*"rebuild and upload and rerun hitl"*). Same registered profile
`STIM-WINDOW-CONTENT-01@v1`, same target shape and the same invocation as the two runs before it.

| item | value |
|---|---|
| build | `teensy41-capture-serial-window-qualification`, **clean rebuild** (`-t clean` then build): SUCCESS in 78.8 s, RAM1 `variables 96,192 / code 392,312 + padding 904` |
| image | `firmware.hex` sha256 `4596d517797c652f92801fa26a455826e91ffd0b0bffe1d47c7e9398fef3e376` |
| tree | `a5b3bfc4` (my record commit) **plus 21 uncommitted tracked files and 3 untracked evidence dirs** — the in-flight `runtime-working-set-ownership` S7.5b work, which now also touches `src/Track/TrackDeferredMaintenance.cpp`, `src/Track/TrackOverdubLifecycle.cpp` and `test/test_runtime_working_set_ownership` |
| capture | `captures/s64-37c-03` |
| references | `s64-37c-02` (previous tree), `s64-37c-01` (committed 3.7c tree), `s64-demand-01` (before 3.7c) |

**Finding about the build itself:** the previous near-instant rebuild of this same tree produced
`firmware.hex` `83ca33d5…`, and the clean rebuild of the same sources produces `4596d517…`. The firmware embeds only
`__DATE__` (no `__TIME__`), and both builds are the same day, so the two images genuinely differ in code: **the
incremental build linked stale objects for the newest edits.** "Upload the current changes" therefore requires a clean
rebuild here — the incremental image is not trustworthy as a statement about the working tree.

## 1. The windowed pattern reproduces (a third time), four times in one run

On the active slot (track 7, slot 3 — 24 bars, span-aligned, demoted, playing, `sv=0` with a current root `rt=1`):

```
 98.416s x=win_requested          sv=0 rt=1 rl=1 dm=1 rq=12      <- the first-window request is ACCEPTED
 98.427s x=win_waiting            ow=1 dm=1 bld=1                <- build live, coverage source owned
 98.460s x=win_blocked_by_request ow=1 dm=1                      <- invalidated 33 ms later
 98.xxx  x=win_blocked            ow=0 dm=1 rl=1                 <- source retired, demand coalesced back
119.067s / 136.318s / 142.892s: the same request -> waiting -> blocked_by_request, three more episodes
```

Four accepted requests, each invalidated within 33–60 ms, each still carrying **`rq=0`** — the term is unreadable, which
is task **3.7d** (prepared-and-reverted). Where the pre-3.7c run reported `win_not_servable` for this state, all three
post-3.7c runs report `win_requested`: the 3.7c behaviour is now reproduced on three runs, two trees and two slots.

## 2. What is different on this tree (flagged, not attributed to the windowed change)

| surface | `s64-37c-02` (previous tree) | `s64-37c-03` (this tree) |
|---|---|---|
| gap blocks with eligible/progress turns | 6 (up to `10/10`) | **0** |
| `win_gap` `running` flag | `1` in the served blocks | **`0` in all 36 blocks** |
| `build_complete` | 1 (the switch target) | 0 |
| readiness receipt | yes (`owes_build`) | yes (`owes_build`, same holder) |
| publication-owner rows | `snap_gate` 452 | **`snap_gate` 963, `snapshot_unit` 878** |

So on this tree the demand is still opened and still invalidated, but the lane's own coverage row reports
`running=0` for the whole session and **no** turn is counted as eligible or progressing, and nothing is published —
while the publication owner is exercised twice as much as before. This is the state of the **uncommitted S7.5b tree**
(which edits `TrackPlaybackWindowBuild.cpp`, `TrackDeferredMaintenance.cpp` and `TrackOverdubLifecycle.cpp`), so it is
reported for that slice's attention rather than attributed to `windowed-playback-generation`; the windowed claim here is
limited to §1's request/invalidation pattern.

**Corrected the same day by a repeat on the same image:** the very next run, `s64-37c-04` (same board, no rebuild, no
flash), shows 33 `running=1` gap blocks, 6 with eligible/progress turns **and a `build_complete` publication** — so the
differences in this section are **not reproducible on the same tree** and must not be read as caused by it. See
[`DEVICE-RUN-04.md`](DEVICE-RUN-04.md). What survives is §1 (the request/invalidation pattern) and the fact that the
active slot still publishes nothing in every run.

## 3. Honest limits

- No completion is claimed on any tree: no `build_complete` publication for the active slot in any of the three runs.
- The reused leg's own oracle failed again, with a **different** issue this time:
  `missing_playing_bounded_window_disp` (previous runs: `unexpected_loop_len`). Its display-window oracle is its own;
  the lane's rows above are unaffected.
- F-7/F-8/AC-8 and 6.5's four hardware claims stay **NOT RUN**; no timing/service-envelope claim; `ExecutionBounds.md`
  rows 27–29 unchanged; the shipping image still compiles the lane off.
- The board now runs the clean-built `4596d517…` image (the mixed tree), not the committed windowed image.
