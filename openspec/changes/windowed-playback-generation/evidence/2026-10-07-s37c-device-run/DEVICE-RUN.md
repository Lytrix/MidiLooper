# Device run of 3.7c: the active slot opens its first window

**2026-10-07, after the owner's direction** (*"do a"* — the active slot's first window as the next slice, with a device run
as its first fixture). Same registered profile `STIM-WINDOW-CONTENT-01@v1`, same authorized bench target (track 8
slot 4) and the same invocation as the previous run, so the **only** variable is the slice: the qualification image
built from `1a61a6a9`.

| item | value |
|---|---|
| image | `teensy41-capture-serial-window-qualification`, `firmware.hex` sha256 `5786cdb23cf226234bf471c02ce3af7b4729d8108624c87d7a5656cd15d86c0e` |
| git | `1a61a6a9` (task 3.7c) |
| capture | `captures/s64-37c-01` |
| invocation | `host_midi_hitl.py run --preset windowed_content_parity --track 8 --loop-slot 4 --midi-channel 8 --record-bars 23 --switch-target-slot 5 --switch-settle-s 45 --serial-port /dev/cu.usbmodem154801 --out-dir captures/s64-37c-01` |
| reference | the same leg, same target, **without** 3.7c: `captures/s64-demand-01` |

## 1. The change is visible on hardware, on the active slot

The active slot (track index 7 = operator track 8, slot index 3 = operator slot 4) is the demoted
(`av=1`), span-aligned (`al=1`), playing (`pl=1`) loop with `len=18432` and, crucially, **`sv=0`** — nothing served,
which is the state that used to end the lane.

| | before 3.7c (`s64-demand-01`) | with 3.7c (`s64-37c-01`) |
|---|---|---|
| exit for that exact state | `x=win_not_servable … sv=0 rt=1 rl=1` | **`x=win_requested … sv=0 rt=1`** |
| the demand | none — refused | opened on the active slot |

The row, verbatim from the capture:

```
t=7,s=3,x=win_requested,len=18432,ep=1,av=1,al=1,pl=1,ed=0,od=0,pv=0,rw=0,sv=0,rt=1
```

and the frame then moves through the lane's own states rather than stopping at a refusal —
`win_waiting`, `win_blocked`, `win_blocked_by_request` (2 rows each), with the entry terms intact
(`ep=1 av=1 al=1 pl=1`). The refusal that remains on the slot is the one that is supposed to remain: `win_not_servable`
with **`rt=0`** (no current source yet), before the root became current.

## 2. The lane served the demand, and where it stopped

```
win_gap elig=4 prog=3 denied=1 run=1 opp=8242     <- the lane's own turns on this shape
win_gap elig=1 prog=1 denied=0 run=1 opp=8448
#CAP,143796393,DIAG,win_readiness,7,4,owes_build,window_build_owner,1,0
```

So the first-window demand was **admitted, served (3 progressing turns, 1 denied turn) and the readiness receipt then
names the holder that still owes the build** (`window_build_owner`) — the demand is alive and progressing, and it did
not complete inside the run's own window (the transport is stopped by the leg's drain shortly after the 45 s switch
settle). The **switch target's** launch chain also went further than in the previous run: `launch_requested` →
`launch_requested_match` → `launch_building_match` → `launch_ready` → `launch_ready_owns_source`.

**The next measured question is therefore narrower than before:** not "does the lane get a demand" (it does, on the
active slot, and it is served) and not "which term refuses" (the entry trio holds; the remaining refusal is `rt=0`
before the root is current) — but **why a first window's build does not complete within the service the leg gives it**
(the `denied=1` turn and the `window_build_owner` receipt are the surface to read; the launch lane's slow-publication
family from `s64-trace-01`/`s64-progress-01` is the same question).

## 3. Honest limits

- **No completion is claimed.** No `build_complete` publication appeared for the active slot in this run, so the slot
  is *not* yet served from a window on hardware; the slice's device claim is exactly what §1 states — the request is
  opened where it used to be refused.
- **The reused leg's own oracle failed again**, differently this time: `window_not_frozen_during_note_edit
  unique_starts=[0, 6144]` (the previous run failed on `unexpected_loop_len`). Both are the reused display leg's oracle
  against its own recipe expectations, not the window lane; the len/geometry of the record itself was fine
  (`len=18432`, `al=1`).
- F-7/F-8/AC-8 and 6.5's four hardware claims stay **NOT RUN**; no timing/service-envelope claim; `ExecutionBounds.md`
  rows 27–29 unchanged; the shipping image still has the lane compiled off.
