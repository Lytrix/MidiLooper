# Fifth run: clean rebuild + upload with everything in the tree, reset, and the first served window on the active slot

**2026-10-07, on the owner's instruction** (*"build firmware with all changes now so far upload and rerun hitl"*). Same
registered profile `STIM-WINDOW-CONTENT-01@v1`, same target shape, same invocation.

| item | value |
|---|---|
| build | `teensy41-capture-serial-window-qualification`, **clean rebuild** (`-t clean`, 71.5 s): SUCCESS, RAM1 `variables 96,192 / code 392,312 + padding 904` |
| image | `firmware.hex` sha256 `278db0291f43b2d9857ce676666fba75e612bc7fe37b376b6eb1f2ae8ff8963b` |
| tree | `5515ba19` plus **27 uncommitted tracked files and 5 untracked entries** — the in-flight work now also touches `include/Utils/DisplayWindowUtils.h` and `test/test_display_window_utils` |
| captures | `captures/s64-37c-05` (failed first attempt), `captures/s64-37c-05b` (the run below) |

## 0. A HITL papercut worth recording: an aborted leg wedges the target track

The **first** attempt never reached recording — `warn: RECORDING not confirmed; retrying record press` then
`error: record did not reach RECORDING` — and wrote no run files at all (its only artifact is a drain log). The drain
names the cause on the target track:

```
#CAP,216219575,ODUB,stop,admission,0,0,4294967295,4294967295,1,0,canonical_output_busy,track=7
```

i.e. an accepted-but-unretired stop obligation on track 7, so the track could not arm again. **Re-uploading the same
image (the documented reset) cleared it**, and the second attempt recorded normally (`record stream notes=368
clocks=2208`, `record stop (returns to play)`). Practical rule for these legs: after an aborted run, reset before
rerunning, and treat "record did not reach RECORDING" as a device-state symptom first.

## 1. The lane served more than in any previous run

| surface | runs 01/02 (3.7c tree) | run 04 (mixed tree) | this run (mixed tree, reset) |
|---|---|---|---|
| gap blocks with eligible/progress turns | 2–6 | 6 | **32** (of 72 blocks, 59 with `running=1`) |
| `build_complete` | 1 (switch target) | 1 (switch target) | 1 (switch target, `loop_len=19200`, 136.056 s, with the `owes_build` receipt) |

## 2. The active slot now shows a **served** window (`sv=1`) — and the roll is still blocked by request

```
 97.705s s=3 x=win_requested          len=18432 sv=1 rt=1 rl=1 dm=1 rq=12   <- sv=1: a window is served
 97.726s s=3 x=win_waiting            sv=1 rt=1 rl=0 ow=1 dm=1 bld=1       <- build live, source owned
 97.800s s=3 x=win_blocked_by_request sv=1 rt=1 rl=0 ow=1 dm=1             <- invalidated 74 ms later, rq=0
131.297s s=3 x=win_requested          sv=1 rt=1 rl=1 dm=1 rq=12            <- again
131.306s s=3 x=win_waiting            sv=1 rt=1 rl=0 ow=1 dm=1 bld=1
131.346s s=3 x=win_blocked_by_request sv=1 rt=1 rl=0 ow=1 dm=1             <- 40 ms later, rq=0
```

Across the five runs the active slot has always carried `sv=0` at its requests (the 3.7c first-window shape). **This is
the first run where the row reads `sv=1`** — *the prewarmed view set's current view is complete and published*, the
flag's own definition — so the request here is the **roll** off an already-served window rather than a first window.
Stated precisely: a window is served for the slot, but **no `win_ready` or `win_serving` row appears for it and no
`build_complete` belongs to it**, so this is a *served view satisfied by some other route* (a bound launch view) rather
than a publication of the lane's own rolling interval. The roll request is then invalidated within 40–74 ms exactly as
before, and still with **`rq=0`** — task **3.7d** remains the thing that makes that term readable.

## 3. Flagged for the in-flight display work (not attributed to the windowed change)

The reused leg's own oracle failed on its **display-window** checks this run — `missing_bounded_window_disp_rows`,
`insufficient_disp_during_note_edit_freeze count=0`, `missing_playing_bounded_window_disp` — where earlier runs failed
on `unexpected_loop_len` or passed outright. Those checks read the `DISP` row family, and the tree's newest edits are in
`include/Utils/DisplayWindowUtils.h` and `test/test_display_window_utils`, so this is worth that slice's attention; the
window-lane rows in §1–§2 are unaffected by the leg's display oracle.

## 4. Honest limits

- No `build_complete` publication belongs to the active slot in any of the five runs; no timing/service-envelope claim;
  F-7/F-8/AC-8 and 6.5's four hardware claims stay **NOT RUN**; `ExecutionBounds.md` rows 27–29 unchanged.
- The board runs the clean-built mixed-tree image `278db029…`.
- This run is the first whose leg captured under the tree's **new display code**, so §3's oracle failures are reported
  as observations about the leg's own display checks, with the attribution left to that slice.
