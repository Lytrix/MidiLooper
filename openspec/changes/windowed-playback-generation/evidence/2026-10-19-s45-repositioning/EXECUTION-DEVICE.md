# Device attempts at the re-positioned demand (2026-10-19)

Image: `teensy41-capture-serial-window-qualification`, `code 392,376 + padding 840` (floor, unchanged).
Run 1 metadata pins git `7276f652` + dirty tree (the implementation) and HEX SHA-256
`8a9ed144f4d2d84672af1aec1c696d41e306f6721f37901e002aa0f7cac3d1ad`; runs 2 and 3 reused that flashed image
(`--no-boot-reset`, `--boot-reset`). All three runs cleared and re-recorded the authorized track 8 / slot 4.

| run | shape | committed loop | lane gate exits | demand? |
|---|---|---|---|---|
| `captures/i5-s64-f9-reposition-24bar-01` | `--build-upload`, record 24 | `RECS,stop,...,18432` (24 bars, `al=1`) | `win_track_not_playing` 84, `win_window_ineligible` 20, `win_not_servable` 12, `win_blocked` 2, `win_busy` 2 | **no** `win_requested` |
| `captures/i5-s64-f9-reposition-32bar-01` | `--no-boot-reset`, record 32 | `RECS,stop,...,24576` (32 bars) | `win_track_not_playing` 72, `win_window_ineligible` 20, `win_not_servable` 8, `win_busy` 6, `win_blocked` 2 | **no** |
| `captures/i5-s64-f9-reposition-boot-32bar-01` | `--boot-reset`, record 32 | restored 32-bar take | `win_track_not_playing` 74, `win_window_ineligible` 20, `win_not_servable` 12, `win_blocked` 2, `win_busy` 2 | **no** |

## What the rows say

The refusal-control capture (`i5-s64-f8-parity-32bar-01`, line 10126) opens its demand with:

```
x=win_requested,len=24576,ep=1,av=1,al=1,pl=1,ed=0,od=0,pv=0,rw=0,sv=0,rt=1,rl=1,ow=0,dm=1,bld=0,gr=0,rp=0,rq=12
```

`pl=1` (playing), `rw=0` (no record-return window), `rt=1` (published root current), `rl=1` (coverage role
available). The three attempts each miss one of those terms at the moment transport starts:

- **24-bar:** the transport start nearest the take finds `rw=1` (record-return bound): `win_blocked` with
  `sv=1, rt=1, rl=0, dm=1`. The lane is gated by the return window, not by a position.
- **32-bar, no boot:** no boot sweep at all; `win_track_not_playing` dominates (72) because the preset leaves
  track 8 stopped between the record leg and the display leg.
- **32-bar, boot:** the boot sweep *is* captured (all eight tracks, `pl=1`, µs ≈ 11 920 000), and for `t=7,s=3` it
  reads `x=win_not_servable, sv=0, rt=0` — the restored take's root is **not current yet** at that instant, so the
  demand is never made. (The refusal-control run had `rt=1` at its sweep.)

So the missing ingredient is a transport start with the demoted target **playing, root current, no record-return
window** — i.e. after the boot restore has completed and after the record-return window has retired. The current
preset's transport starts happen outside that window (too early after boot, too early after stop, or with the track
stopped).

## Next move for the device proof

1. Add a settle-and-replay leg to the registered preset (or a temporary variant) that, **after** the restore is
   complete and the record-return window has retired, selects track 8 and starts the transport with slot 4 playing.
   The precondition to assert before pressing play: `pl=1` and `rt=1` and `rw=0` in the lane's gate row (a
   `win_not_servable` with `rt=0`, or any `rw=1`, means the leg ran too early).
2. Then read, per episode: `win_requested` → `handoff_unreachable` / `build_drop` → `win_repositioned` →
   `win_requested` → `win_ready` → `win_serving`, plus drain continuity.
3. Keep the 24/32-bar high-contention captures as the refusal control; no new stimulus, no hand-rolled MIDI bytes.

No claim is made that the re-position works on device yet. The native RED is green (217/217 focused; 3,334 full,
7 skipped, 0 failed) and the image is pinned above.
