# Device run: the demand's entry terms hold, the lane serves, and the refusal is now one named term

**2026-10-07, authorized by the owner** (*"Yes — flash and run the leg now"*, target *"Track 8 slot 4"*). Registered
profile `STIM-WINDOW-CONTENT-01@v1` (`windowed_content_parity`) through the existing content-verification pipeline; no
canonical stimulus edited. Capture: `captures/s64-demand-01`.

| item | value |
|---|---|
| image | `teensy41-capture-serial-window-qualification`, `firmware.hex` sha256 `8e558cd7ff846d6843d074522064707898cae602c325adbba83e8954825961fb` |
| git | `71bcfe2c3062998abc371f1108cc3db02eda8090` |
| invocation | `host_midi_hitl.py run --preset windowed_content_parity --track 8 --loop-slot 4 --midi-channel 8 --record-bars 23 --switch-target-slot 5 --switch-settle-s 45 --serial-port /dev/cu.usbmodem154944801 --out-dir captures/s64-demand-01` |
| gesture | clear + record slot 4 (track 8), NOTE_EDIT window checks, exit; then the **switch leg** to slot 5 and the post-stop drain |

## 1. The demand exists on hardware, and its entry terms hold

For the first time in this change's device work the lane was **given work**: the gap blocks carry eligible turns and
`denied=0`, and a readiness receipt was emitted under transport.

```
#CAP,150571533,DIAG,playback_build,build_complete,0,19200,0,1,7,4,0,0,4294967295,0,0   <- a published window (t=7 s=4)
#CAP,150571679,DIAG,win_readiness,7,4,owes_build,window_build_owner,1,0                <- readiness, under transport
win_gap 150571696 opp=10728 run=1 eligMax=11 elig=10 prog=10 denied=0 workerUs=3 permitted=64 schema=3
win_gap 150838639 opp=10752 run=1  eligMax=4  elig=1  prog=1  denied=0 workerUs=3 permitted=64 schema=3
```

`verify_windowed_lane` over the combined capture: `window_demand_observed` **true**, `readiness_receipts_under_transport`
**pass**, `windowed_service_coverage_under_transport` **pass**, `no_windowed_overrun_under_transport` **pass** — the lane
served under transport with **zero** exceedances of the declared `permitted=64`.

**The entry trio that five earlier runs could not satisfy now holds** (`ep`/`av`/`al` all `1`) on the leg's own target:
the record produced **19968 ticks = 26 bars**, and `19968 % 1536 == 0`, so the recorded loop is demoted *and*
span-aligned:

```
t=7 s=3 x=win_not_servable len=19968 ep=1 av=1 al=1 pl=1 ed=0 od=0 pv=0 rw=0 sv=0 rt=0 rl=0
t=7 s=3 x=win_not_servable len=18432 ep=1 av=1 al=1 pl=1 ed=0 od=0 pv=0 rw=0 sv=0 rt=1 rl=1
```

## 2. What still refuses, by name — one term, not a mystery

Every refusal on a demoted, aligned, playing slot is the **servability** term, never the entry trio:

```
t=7 s=3  len=19968 av=1 al=1 pl=1 -> win_not_servable sv=0 rt=0
t=7 s=3  len=18432 av=1 al=1 pl=1 -> win_not_servable sv=0 rt=1 rl=1
t=0 s=0  len=15360 av=1 al=1 pl=1 -> win_not_servable sv=0 rt=1
t=4 s=0  len=18432 av=1 al=1 pl=1 -> win_not_servable sv=0 rt=0
```

`sv=0` is *the active slot holds no complete published current window*, so the rolling lane has nothing to roll from —
including where the root **is** current (`rt=1`) and the coverage role **is** available (`rl=1`). The remaining `win_gate`
exits are correct and other shapes: `win_window_ineligible` on loops that are **not** demoted (`av=0`, 1/2/9-bar loops on
other tracks) or unaligned, and `win_track_not_playing` before the transport/while an edit session was open.

**The one window that was published is the switch target's** (`t=7 s=4` = operator track 8 **slot 5**, `loop_len=19200`),
and the switch that would have made it serve **held at its bound** — the same hold the earlier traces recorded:

```
[150.304] Loop 5: Queued playback switch
[158.234] Slot switch hold slot_switch_hold_exceeded track=7 target=4 age=3072 ticks
```

with the readiness receipt beside the publication naming the holder that still owes it (`owes_build`,
`window_build_owner`). So the chain reads: the **build** lane works on hardware (a window built and published, served
under transport, no overrun, eligible turns non-zero, a receipt emitted), while the **serving** path for the *active*
slot still never receives a first window, and the switch that would deliver one for a demoted target holds.

## 3. The verifier's and the leg's own verdicts

* Lane half: **one** failure — `window_boundaries_crossed_under_transport` (`build_complete_under_transport=1`, needs ≥2,
  `loop_len=19200`). That is the same statement as §2 in one line: one publication, not a crossing.
* The reused leg's own oracle failed on its **length expectation**: `unexpected_loop_len expected≈17664
  observed=[19968]`. This is a **host expectation**, not a firmware defect: the run asked for `--record-bars 23` and the
  device's own record-stop quantisation committed **26 bars** (which is what made the loop span-aligned, the property the
  demand needs). The reused leg derives its expectation from the requested bar count, so the shape and the oracle now
  disagree; reconciling that belongs with the leg (its `--record-bars` → committed-length relationship), not with the
  lane.
* `oracle_complete: False` as always: F-7/F-8/AC-8 qualification is **not** claimed, no timing/envelope claim is made,
  and `ExecutionBounds.md` rows 27–29 still deny active windowed playback.

## 4. What changed relative to 2026-10-05, and what the next step is

| | 2026-10-05 (last windowed device work) | this run |
|---|---|---|
| demand | none: 44 `win_gap` blocks, all `eligible=progress=0`, **0** receipts | **2 blocks with eligible/progress turns, 1 receipt under transport** |
| entry terms | `al=0` (25-bar commit) — the demand could not exist | `ep=1 av=1 al=1` on a 26-bar span-aligned commit |
| named refusal | `win_window_ineligible al=0`, and `win_not_servable` where aligned | `win_not_servable sv=0` alone, on every demoted aligned playing slot |
| publications | 0 | 1 (`build_complete t=7 s=4 len=19200`) |

**The next step is therefore no longer "get a demand" — it is the active slot's first window.** Two candidate owners,
both already on the record: the launch's source publication for a demoted target (s64-trace-01's *admitted idle
producer*, and this run's `slot_switch_hold_exceeded` + `owes_build` receipt), and §2 of the change's own reconciliation
(initial launch is production-wired, and the switch binds it). No firmware was changed in this run and none is proposed
here.
