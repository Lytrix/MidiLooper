# Route reject on device — task 2.3 (2026-11-01)

## Run

- **Image.** Default `teensy41-capture-serial`, HEX
  `1799e442719f1808879aa41679c4dd15314f91a4b1fffa67d35ef63394a72aae` (commit `de21b9bd`). Upload SUCCESS; boot proven
  in the capture (`DIAG,boot_reset,crash=0`, `BOOT,load,ok`).
- **Rig.** Owner chain: `hitl_midifile_chain.py --hold-clock-on-settle --stop-bars 8 --record-bars 20`, 16ths/chords,
  then a 45 s internal-clock hold. Output in `captures/midifile-chain-60`.
- **Capture.** `captures/session_20261101_114703.log`. The chain is valid: 1 Record, 12 overdubs, 0 `order_deny`, 0
  `preparation_denied`.
- **Ring.** `RING,stats` shows `tierA_evict=0, overflow=0, crit_rej=0, wipe=0`, so the Tier-A rows are complete.
- **Decoder.** `scripts/parse_tick_routes.py captures/session_20261101_114703.log`.

## Routes (for comparison with the phase-1 baseline)

| Interval | windows | view | merged | none | miss_int |
|---|---|---|---|---|---|
| external | 25 | 5,933 | 11,920 | 0 | 0 |
| internal | 11 | 20,714 | 41,879 | 2 | 2 |

- Merged share is 66.9 % of 80,448 slot-ticks, against 85.6 % in the baseline (a different card state).
- The internal-clock misses are 2, against 356 in the baseline.
- Bridge and paged delivery are still never used.

## Reasons: the whole timeline (4 rows)

| t (s) | slot | loop | reason | producer's last exit |
|---|---|---|---|---|
| 14.077 | t0 s0 | 1 bar (768) | `frame_idle` | `win_track_not_playing` |
| 14.077 | t1 s0 | 8 bars (6,144) | `frame_idle` | `win_track_not_playing` |
| 66.279 | **t7 s3** | **20 bars (15,360)** | **`clock_epoch_changed`** | `win_track_not_playing` |
| 66.334 | **t7 s3** | 20 bars | **`frame_blocked(clock_epoch_changed)`** | **`win_frame_invalidated`** |

There are no further changes through the end of the capture, which spans:
- 12 overdubs (68.7–111.6 s);
- a transport stop and restart (123.46 / 123.77 s);
- the switch to the internal clock (139.87 s);
- the 45 s internal hold.

## Finding: the long loop (the acceptance shape)

**The first failed predicate is `ClockEpochChanged` in `Track::resolveRollingPlaybackWindow`.**

1. The 20-bar slot's rolling frame carries the epoch of the earlier transport, when the frozen Record reader served it
   (`win_frozen_readiness … route`).
2. At 66.278 s the transport starts again (`Transport started`, after the post-record start at 54.435 s), which
   re-stamps the clock epoch.
3. The very next tick fails the epoch term, so `rejectRollingFrame` blocks the frame.
4. The producer then reads the blocked frame as `win_frame_invalidated`. That exit sets `Blocked` and returns, keeping
   "the original debt; do not grant a new clock frame".
5. **No path in the trace leaves that state.** The slot stays `frame_blocked(clock_epoch_changed)`, served by the merged
   fallback, for every later tick of the run: every overdub, the stop/restart at 123 s, and the whole internal-clock
   hold.

This is a **routing/admission** shape, not yet a capability verdict. No merged tick was filed for this slot before 66.28 s (its first filing would have
emitted a row), so it was served by a non-merged reader until then; what is missing is the recovery from an epoch change. Phase 3 must still prove capability separately (design D3).
Phase 4's candidate repair is to make an epoch-invalidated frame re-request instead of blocking it permanently. That
is a design decision for task 4.1 against DEC-193.

## Finding: the short restored loops (t0, t1)

- The 1-bar and 8-bar slots playing since boot are merged from their first tick, with `frame_idle`.
- The rolling producer does not open demands for loops of 16 bars or fewer (`shouldAvoidFullVisualRebuild`, i.e.
  `win_window_ineligible`). By design those loops are served by a **published full-loop view or bridge**
  (`resolveFixedPlaybackSourceForTurn`), which returned nothing for them.
- **Gap in the phase-2 enum:** that full-loop-view miss sits before the rolling resolver in decision order and is not
  named. These rows therefore report the resolver's first exit (`frame_idle`) instead. Naming it needs one
  append-only value.
- This is outside the long-loop acceptance criterion, but it is a large share of the merged ticks.

## Caveats

- The `DIAG,route_reject` emitter suppresses a reason that changes and returns within its 50 ms spacing. Its `chg`
  count advances silently, and no such row appears here.
- The producer token is the producer's last exit **at emission time**, not a history.
- Per-slot merged-tick totals are not attributed for slots whose reason never changed. The only closed count is
  `clock_epoch_changed = 1` tick.
