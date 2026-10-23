# 3.7d — device read attempt: the blocked-demand exit did not reproduce, and 32 bars cannot prove the hold term

Task: **3.7d** (`tasks.md`), whose increment landed 2026-10-20 (`42ac51f6`) and whose remaining obligation is a device
run that reaches `win_blocked_by_request` **and reads a named term** in `rq`. Capture:
`captures/i5-s37d-device-32bar-01` (gitignored). Image: `teensy41-capture-serial-window-qualification`, HEX
`3ef153c5deb5f5337675ca96bb2b75872730f2efd7eab6b30e1a961c1c8bffdc` (current tree, includes the 3.7d increment and the
6.4 receipt). Invocation: the registered preset `windowed_content_parity`, track 8 / slot 4 / channel 8,
`--record-bars 32 --no-boot-reset`, no flash (the board already ran this image).

## Why 32 bars

The condition is state-dependent and the one capture that ever showed the exit used a **32-bar** take:
`captures/i5-s64-f8-parity-32bar-01/serial.log` (2026-10-19, pre-increment) has a single
`win_blocked_by_request,len=24576,…,rl=0,…` row, 87 µs after `build_drop,released_without_site` and 10 ms after
`build_drop,handoff_unreachable`. Pre-increment `rq` on that token was `frame.refusedSpanAttempts` (`0` there), which is
exactly the "five candidates and no reader" the increment removes.

## Result — not reproduced

- **`win_blocked_by_request`: 0 rows** in the session log, the leg log and the post-stop drain (18 426 lines).
- The lane never **requested** a window in this run: `win_requested` **0**, `win_repositioned` **0**, `win_ready` **0**,
  `win_serving` **0**. The `t=7,s=3` rows are `win_track_not_playing`, `win_window_ineligible` (20),
  `win_not_servable` (10) and `win_busy` (6), all with `bld=0, gr=0` — nothing pending, so the exit's precondition (a
  retained request that is position-invalidated or carries a terminal term) was never met.
- The take is the intended size: `Recording stopped @ tick 209 (recStart=0 length=24576)`, i.e. 32 bars, aligned
  (`24576 % 1536 == 0`), `av=1 al=1 rt=1` on the active slot. So the difference from 2026-10-19 is the lane's own state at
  the moment of the demand, not the configuration.

**Conclusion:** the device read stays owed. This attempt narrows it: a 32-bar take alone does not produce the exit; the
2026-10-19 episode needed the request to be admitted **near the loop end** (`phase 24599` of 24576, one tick before the
wrap) so the contract could not finish before its handoff, and that timing did not recur in a record-then-play run.

## Recipe limit measured on the way (independent of 3.7d)

The run's leg went red on `play_stop_hold_did_not_track_playhead unique_starts=[12288]`, and the receipts say why:

```
w=6164 b=16 → w=6916 → w=7416 → w=10758 → w=11523 → w=12288 → w=12288 (hold) → w=12288 (STOPPED)
```

`resolveCenteredWindowStart` centres the window and `resolveDetailedPaintWindow` clamps it to
`L − windowLength`; the paint's own clamp and the 24-bar runs agree with that (`w` never exceeds `18432 − 12288 = 6144`
there). At **32 bars** the clamp is `24576 − 12288 = 12288`, reached once the playhead passes **24 bars**
(`playhead − 8` bars catches up with the clamp) — and this run's hold sat past that point, so the 4 s hold (2 bars at
120 BPM) had no travel left. The hold term is therefore unprovable at 32 bars once the playhead is beyond 24 bars, and at
**24 bars** the same clamp lands at playhead 16 bars (travel = 8 bars). **24 bars remains the leg's configuration**, and
the leg's gestures land before the clamp, which is why 03/04/05 prove the hold there.
