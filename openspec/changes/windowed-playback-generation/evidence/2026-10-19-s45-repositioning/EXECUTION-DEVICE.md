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

The preset already owns the missing shape, and it was **off** in all three runs: `switch_leg`
(`scripts/hitl/scenarios/windowed_content_parity.py:206`) exists precisely to "give the window lane a **demand** under
transport" -- "a plain record-and-play owes no window (measured), while a switching target owes one because the launch
prepares it" (`switch_target_slot_from`, `:158`). It is driven by `--switch-target-slot N` and is absent by default so
the base shape stays comparable. The three attempts above are therefore the *base* shape, and their readings agree with
that comment.

1. Run the same preset with the switch leg enabled (`--switch-target-slot N`) against a filled, demoted, **non-active**
   slot, so the launch binds a first window for it. Keep `--record-bars 32` and the no-boot-attach shape.
2. Assert the entry precondition on the wire before reading the outcome: the gate row for the switch target must read
   `pl=1`, `rw=0`, `rt=1` (the refusal control's `win_requested` row is `pl=1, rw=0, sv=0, rt=1, rl=1, dm=1`). A
   `win_not_servable` with `rt=0`, or any `rw=1`, means the leg again ran outside the window and is not a result.
2. Then read, per episode: `win_requested` → `handoff_unreachable` / `build_drop` → `win_repositioned` →
   `win_requested` → `win_ready` → `win_serving`, plus drain continuity.
3. Keep the 24/32-bar high-contention captures as the refusal control; no new stimulus, no hand-rolled MIDI bytes.

No claim is made that the re-position works on device yet. The native RED is green (217/217 focused; 3,334 full,
7 skipped, 0 failed) and the image is pinned above.

## Attempt 4 — the demand fires; the retry does not converge (2026-10-19, lane image `8a9ed144…`)

Two runs were added, both on the pinned lane image (`8a9ed144f4d2d84672af1aec1c696d41e306f6721f37901e002aa0f7cac3d1ad`,
git `7276f652` + dirty):

| run | shape | result |
|---|---|---|
| `captures/i5-s64-f9-switch-32bar-01` | `--record-bars 32 --switch-target-slot 4` | no demand: every row with `pl=1` also reads `rw=1` (the record-return window is still bound while the take plays), and the 92 `win_track_not_playing` rows are the stopped stretches. The switch to the **already active** slot queues nothing. |
| `captures/i5-s64-f9-replay-32bar-01` | `--record-bars 32 --post-stop-start-s 6` | the replay flag refused itself ("this capture ends with it running"), so no extra start was pressed — **and the demand appeared anyway**, inside the display leg. |

The replay run is the one that answers the question. The demand's entry row is exactly the refusal control's shape:

```
x=win_requested,len=24576,pl=1,rw=0,sv=0,rt=1,rl=1,dm=1,rq=12
```

and the episode then repeated, 231 times, over 15.7 s (482.725 → 498.414 s device time, one attempt every ~68 ms):

```
win_requested → win_waiting → handoff_unreachable (build_drop) → win_repositioned → win_busy → …
```

Census for the run: `win_requested` 232, `win_repositioned` 231, `win_waiting` 231, `win_busy` 232, **`win_ready` 0,
`win_serving` 0**, `win_blocked` 1 (at the end, when the phase changed and the loop stopped). So the device proved the
re-position **fires** and the ordered chain is reachable up to the refusal, but the retry never converged: every retry
was refused with the same ~40 ms signature and no terminal row was emitted while the traversal cycled.

### Why it did not converge

The retry position is computed from the moving playhead on every attempt. The device arithmetic is small enough to
reproduce natively: with the declared lookahead (`7 grains × 8 denied turns × 64 ticks + 768 = 4,352` ticks) the retry
is placed at `phase + lookahead` rounded up to the span. The shift to the refused handoff compared **numeric** phases
(`startPhase < refusedHandoffPhase`), and once `startPhase` wrapped past the loop length that comparison is false while
the position is still a whole traversal behind the handoff — so the shift was skipped, the retry fell back to the same
marginal lead, and the cycle repeated. Measured natively (the new fixture, before the fix): 128 re-positions whose
chosen start advanced ~100 ticks instead of one span each.

The old traversal guard compared `boundary - phase`, which is re-derived from the moving playhead on every retry and
therefore never grows by a traversal. It can only expire on elapsed **ticks**, which at the device's 68 ms cadence would
allow ~900 attempts per traversal before the terminal row — the "same-handoff retry loop" the contract forbids.

### Fix applied (DEC-147, second form)

- The shift measures the refused handoff as a **forward** distance from the playhead (`(h − phase) mod len`) instead of
  comparing numeric phases, so a wrapped start is still placed at the refused span's handoff.
- The traversal budget is counted in **spans** (`refusedSpanAttempts ≥ loopLength / span`), because that is the quantity
  the contract is written in. The counter lives in the rolling frame's existing padding (both ABI pins unchanged:
  7,896 target / 10,080 host).
- Native RED is green: `test_a_traversal_of_handoff_refusals_ends_terminal_instead_of_looping` holds the too-close
  condition for a whole episode and asserts one attempt per span, the terminal `win_blocked_by_request`, `Blocked`, and
  `win_serving == 0`. The re-position-success and stale-demand fixtures are unchanged and still pass.

## Attempt 5 — the corrected retry converges to `win_ready` (2026-10-19, lane image with the span bound)

Same entry point as attempt 4 (plain `--record-bars 32 --no-boot-reset`, no switch and no replay flag), rebuilt with the
forward-distance shift and the span budget: `captures/i5-s64-f9-spanbound-32bar-01`, HEX
`45db64ce7c781c4b9e810ee25e1d5f4dd9402f1a6da9c5b853f708eac27e3942`, git `76392a79` + dirty. The lane image is unchanged on
RAM1 (`code 392,376 + padding 840`, the floor) and the full native suite is **3,335 / 7 skipped / 3,328 succeeded /
0 failed**.

The demand opened with the same entry shape and then **converged, for the first time on device**:

```
win_requested  (pl=1, rw=0, sv=0, rt=1, rl=1, dm=1, rq=12)
win_waiting    (rl=0, ow=1, bld=1)
win_repositioned ×20   (one attempt per span, each one span further)
win_ready      (2162735727 µs; rl=0, ow=1, dm=0)
```

The verifier's lane section reads, on this run alone:

| check | verdict |
|---|---|
| `window_demand_observed` | **ok** (first time) — `win_requested`/`win_waiting`/`win_repositioned`/`win_ready` present |
| `readiness_receipts_under_transport` | **ok** — 31 receipts, all under transport, 57 of 356 frozen receipts source-qualified |
| `windowed_service_coverage_under_transport` | **ok** — 519 blocks with opportunities, `permitted=64` |
| `window_boundaries_crossed_under_transport` | **ok** — 475 `build_complete` under transport |
| `no_windowed_overrun_under_transport` | **ok** — 0 overruns |
| `capture_evidence_ring_intact` | **FAIL** — one `wiped` row (`critical_rejected=0, overflow=0, tier_a_evicted=0`) |

### What this proves, and what it does not

**Proves:** the re-position is a real position advance (each retry moves the chosen start one span, so no handoff is
measured twice), and it terminates in the lane's own readiness receipt rather than looping. The 20 attempts are inside
the span budget (`24576 / 1536 = 16` per episode; the frame's episode restarted once when the gate re-opened, which is a
new demand, not a repeated one).

**Does not prove:** `win_serving`. The ready window was prepared at 2162.735 s, the transport ran until 2176.239 s — three
2-bar adoption boundaries (~4 s each) — and no `win_serving` row was emitted. So the chain is now
`win_requested → win_waiting → win_repositioned → win_requested → win_ready`, and the missing edge is **adoption of a
ready successor**, not the demand's position. That is the next slice; the 4.4 demoted-length parity sweep still rides on
a serving image.

The ring check's single `wiped` row is a capture-evidence condition (one Tier-A row overwritten before the drain), not a
lane verdict; it is recorded here rather than worked around, and the run's lane rows are otherwise complete.

## Attempt 6 — the ordered chain closes on a flashed image: `win_ready → win_serving` (2026-10-20)

**Why this attempt exists, and what it corrects.** Attempts 5 and the runs after it were launched with `--build-env` but
**without `--build-upload`**, and the runner only flashes with `--build-upload`
(`scripts/hitl/runner.py`, `maybe_build_upload`). Those runs therefore measured whatever image was already on the board,
which is why a 572-reposition run appeared to contradict the span budget that the native fixture and the code both
enforce: the board was running the **first form**, whose tick-based bound allows hundreds of attempts. Two rules follow,
and they are now part of the recipe:

1. **A device claim names the image that was flashed, and the run that produced it used `--build-upload`.** A run without
   it is evidence about the board's existing image only.
2. **The take must be in RAM for a play-only opening.** A fresh flash loses the in-RAM take; with no content the opening
   playback raises no demand (`win_track_not_playing`/`win_not_servable` only). The sequence used here is: flash + record
   (`captures/i5-s64-f9-markprobe2-32bar-01`, aborted on a leg check but recording `length=24576`), re-record on the same
   image to land exactly 32 bars in RAM (`captures/i5-s64-f9-rearm-32bar-01`,
   `Recording stopped @ tick 667, length=24576`), then the measured run with **no** upload and `--no-boot-reset`.

**The measured run:** `captures/i5-s64-f9-adoption4-32bar-01`, `--preset windowed_content_parity --track 8 --loop-slot 4
--midi-channel 8 --no-boot-reset --record-bars 32 --adoption-hold-s 130`. The hold keeps the transport running for 130 s
immediately after the leg starts it, while the slot's own take plays back with **no record window bound** (`rw=0`) — the
only shape in this leg where a rolling demand and its adoption boundary can both fall inside one continuous run.

**The ordered receipts, as emitted:**

```
win_requested   (pl=1, rw=0, sv=0, rt=1, rl=1, ow=0, dm=1)   rq=12
win_waiting
win_repositioned ×45   (ordinal 1..13, one attempt per span)
win_ready       1261.252 s   (sv=0, rl=0, ow=1, dm=0)
win_serving     1324.060 s   (sv=1, rl=1, ow=0)      <- 62.8 s later, one traversal
win_ready       1329.167 s   (sv=1, rl=0, ow=1)      <- the next successor is prepared while serving
win_serving     1388.073 s
win_ready       1393.189 s
```

**Verifier — lane section, all green (first time):**

| check | verdict |
|---|---|
| `capture_evidence_ring_intact` | **ok** — `critical_rejected=0, overflow=0, tier_a_evicted=0, wiped=0` |
| `window_demand_observed` | **ok** — `win_requested=48, win_repositioned=45, win_ready=3, win_serving=2`, 169 gap blocks with eligible/progress turns, 32 readiness receipts |
| `readiness_receipts_under_transport` | **ok** — 32 receipts, all under transport, 88 of 464 frozen receipts source-qualified |
| `windowed_service_coverage_under_transport` | **ok** — 273 blocks, 201 under transport with opportunities, `permitted=64` |
| `no_windowed_overrun_under_transport` | **ok** — 0 overruns |
| `window_boundaries_crossed_under_transport` | **ok** — 115 `build_complete` under transport |

`lane.ok = true`, `leg.ok = true`, `oracle_complete = false` (4.5 and 6.4 stay open).

### What the timings settle

**Adoption is once per traversal, at the ready window's own start phase** — not at the next 2-bar boundary. The 62.8 s
between `win_ready` and `win_serving` is one loop length (24,576 ticks at 384 ticks/s); `rollingCandidateReadyAtBoundary`
requires `phase == frame.startPhase`, and the start phase is the span the successor was built for. Attempt 5's
"ready, then cancelled 13.5 s later" was therefore never an adoption failure: the boundary was a traversal away, and the
leg's clear/record stopped the transport first. **A serving receipt needs the transport to run at least one loop after
readiness, with no stop in between.**

**The re-position cadence is one attempt per span.** Ordinals advance 1..13 across ~30 s of the hold (≈2.3 s per span,
the 2-bar span at this tempo) and the budget for a 32-bar loop is `24,576 / 1,536 = 16`. The earlier runs' "231 in
15.7 s" and "572 in 30 s" are the first form's tick-bounded retry, not this one.

### Evidence hygiene

`win_pre_guard` (a temporary unconditional row) and the `0x80 | ordinal` marker on `win_repositioned` were probes for
this run and are removed in the commit that records it. The permanent diagnostics are the `rq` fields already carried:
`win_repositioned` → the attempt ordinal, `win_retry_ordinal` → the guard's input (once per distinct value),
`win_blocked_by_request` → the ordinal it decided on.
