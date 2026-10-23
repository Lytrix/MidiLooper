# Fourth run, same image: no upload, and run 03's anomaly does not reproduce

**2026-10-07, on the owner's instruction** (*"rerun the hitl without a new upload"*). **No rebuild and no flash**: the
board kept the image the previous run put on it, `firmware.hex` sha256
`4596d517797c652f92801fa26a455826e91ffd0b0bffe1d47c7e9398fef3e376` (clean rebuild of the current, uncommitted mixed
tree). Same registered profile `STIM-WINDOW-CONTENT-01@v1`, same target shape, same invocation.

| item | value |
|---|---|
| capture | `captures/s64-37c-04` |
| image | `4596d517…` — **identical to** `s64-37c-03`'s image |
| leg's own oracle | **ok=True** (the first of the windowed runs whose reused-leg oracle passes: previous runs failed on `unexpected_loop_len` / `missing_playing_bounded_window_disp`) |

## 1. Run 03's difference is a per-run effect, not a tree effect

| surface | `s64-37c-03` (same image) | `s64-37c-04` (same image) |
|---|---|---|
| `win_gap` blocks | 36, **all `running=0`** | 54, **33 `running=1`** |
| blocks with eligible/progress turns | **0** | **6** |
| `build_complete` | **0** | **1** (`t=7 s=4`, `loop_len=19200`, at 165.322 s, together with the `owes_build` readiness receipt) |

The same image therefore produces both pictures, so
[`DEVICE-RUN-03.md`](DEVICE-RUN-03.md) §2's difference is **withdrawn as an attribution to that tree** (the file carries
the correction in place). The `running` flag and the eligible-turn counters are per-run observations here, not a
property of the current changes — worth knowing before anyone acts on run 03's table.

## 2. What is stable across all four runs

**The 3.7c request/invalidation pattern**, on the active slot (track 7, slot 3; 24 bars, span-aligned, demoted, playing,
`sv=0` with a current root) — two episodes this run:

```
137.728s x=win_requested          sv=0 rt=1 rl=1 dm=1 rq=12      <- ACCEPTED
137.751s x=win_waiting            ow=1 dm=1 bld=1                <- build live, source owned
137.813s x=win_blocked_by_request ow=1 dm=1                      <- invalidated 62 ms later, rq=0
137.836s x=win_blocked            ow=0 dm=1 rl=1                 <- source retired, demand coalesced back
160.552s / 160.561s / 160.608s    the same request -> waiting -> blocked, 85 ms later
```

* **The request is accepted four runs in a row** (two trees, two slots) where the pre-3.7c image refused it here.
* **The invalidation term is still unreadable**: every `win_blocked_by_request` row carries `rq=0` — task **3.7d**.
* **The active slot has never published a window** in any of the four runs. The only publication any run has produced
  belongs to the **switch target** (`t=7 s=4`), and this run is the second one to show it (with the `owes_build` receipt
  naming `window_build_owner` as still owing its window).

## 3. Honest limits

- No timing/service-envelope claim; F-7/F-8/AC-8 and 6.5's four hardware claims stay **NOT RUN**; `ExecutionBounds.md`
  rows 27–29 unchanged; the shipping image still compiles the lane off.
- The board still runs the mixed-tree image `4596d517…` (this run added nothing to the board).
- The `win_gap` `running`/eligible counters differ between two runs of one image; that variability is recorded here as
  an observation with no attribution, and it is the reason run 03's table is not used as evidence about the tree.
