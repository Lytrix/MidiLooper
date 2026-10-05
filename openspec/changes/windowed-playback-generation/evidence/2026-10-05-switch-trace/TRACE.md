# The discriminating episode: one controlled switch into an inactive, committed, even-length, demoted target

Authorized device work, 2026-10-05, by the **registered** profile `STIM-WINDOW-CONTENT-01@v1` (`windowed_content_parity`)
through the existing content-verification pipeline. No new diagnostics: the trace is read from the three rows this change
already has — `DIAG,launch_gate` (the launch path), `DIAG,snap_gate` (the `Snapshot` owner's selector), `DIAG,win_gate`
(the rolling demand path) — plus the device's own publication rows (`DIAG,snapshot_unit`) and its slot-switch report.

**Firmware: unchanged** (`teensy41-capture-serial-window-qualification`, `firmware.hex` sha256 `cd2ca8c52e53d7fc…` — the
same image as `captures/s64-win-gate-05`, so this episode differs from that one in exactly one variable: the target's
length). Host tooling changed only in the bounded F-17 fix (`§5`).

## 1. The target, and its identity as the device reports it

Arranged with the profile's **registered** recipe (`--record-bars 24`, no deviation) into the authorized scratch slot
(operator track 8 slot 6 = firmware `t=7,s=5`):

| step | capture | device row |
|---|---|---|
| preparation run | `captures/s64-etarget-02` | `Recording stopped @ tick 4021 (recStart=3658 length=18432)` — **24 bars = 18432 ticks**, i.e. a whole number of two-bar spans (`18432 % 1536 == 0`) |
| then re-flash | — | the library restores from the card, so the target is **cold and inactive** at the episode's start (`RRQ,7,5,…,terminal,none,committed` at 9.4 s = restored **pass-backed, rootless**) |
| episode run | `captures/s64-trace-01` | the switch leg's own presses: `Loop 6: Queued playback switch` (146.497 s) then `MIDI Encoder: Long press - exited edit mode` (147.349 s) — the record press follows the edit exit, and **no** `Loop 6: Start Recording` row exists anywhere |

The device's own rows then verify the target's identity **before** the decisive transition:

```
DIAG,launch_gate,t=7,s=5,x=launch_requested,len=18432,prep=0,root=0,pend=1,role=1,own=0,dem=0,bld=0,cpm=0,avoid=1,cmt=1,req=1
DIAG,snap_gate,  t=7,s=5,a=4,u=2,p=1,sep=1,fc=0,wj=0,hp=0,lr=1,vc=1,rec=0,play=1,od=0,step=1,cl=1,idle=1,elig=1
```

`len=18432` **even/span-aligned**, `cmt=1` **committed**, `avoid=1` **demoted** (> 16 bars), `a=4` **inactive** (the
active slot is 4), `hp=0` **no published root at all** (the target is rootless, as its restore says). That is the
requested target shape, named by the device rather than assumed.

## 2. The trace, in device order, and the first failed transition

| # | transition | what the rows say | verdict |
|---|---|---|---|
| 1 | **switch pending** | `[146.497] Loop 6: Queued playback switch` (firmware`target=5` on the hold row); no record start on the target | **OK** |
| 2 | **launch preparation** | `launch_gate … x=launch_requested`, `prep=0→1`, `req=1` (the republish request was **accepted**: the source is now owed), `root=0`, `pend=1` | **OK** — the launch runs and asks for its source |
| 3 | **source publication** | `snap_gate … s=5, p=1, sep=1, elig=1, fc=0↔1` — the owner **selects the target** and is **admitted**, with a construction in flight — for **136 rows over ~22 s**; `launch_gate … x=launch_requested_match, prep=1, dem=1, root=0` is the *last* launch exit in the whole capture; **`snapshot_unit` rows: 0 between 146.4 s and 168.6 s**, then 66–80 per 2 s once the transport stops | **✗ FAILED HERE** |
| 4 | initial view ready / bound | never reached: `launch_ready` / `ready_owns_source` appear **nowhere** in the capture; `root=1` appears on **no** launch row | not reached |
| 5 | rolling request | `win_gate,t=7,s=4,x=win_track_not_playing,…,pl=0` (3 rows, the *active* slot 4) — no `win_requested` for the target | not reached |
| 6 | preparation / adoption | no `win_serving`, no `build_complete`, no commit | not reached |

**The first failed transition is (3) the source publication**, and its actual gate state is:

* **the launch is waiting on a request it already had accepted**: `x=launch_requested_match`, `prep=1` (Requested),
  `dem=1`, `root=0`, `pend=1` — the loop itself says a publish is owed and in flight;
* **the `Snapshot` owner is not refusing**: it selects the target (`s=5`, 135 of 136 rows) with `p=1`, `sep=1`,
  `elig=1`, `idle=1`, `step=1` — i.e. admitted, not gate-blocked;
* **and no publication work is serviced while the transport runs**: zero `snapshot_unit` rows in the whole 22 s the
  switch is pending (146.4 → 168.6 s); the units begin only after the wrapper's post-leg stop (168–180 s at ~35–40
  units/s), and even then the target's `root` never becomes current;
* **the switch then reports its bound and stays pending** — `[154.452] Slot switch hold slot_switch_hold_exceeded
  track=7 target=5 age=3073 ticks` (≈4 bars) — a **report, not a decision**: `snap_gate a=4` shows the active slot never
  moved.

So: the switch, the launch and the selector all do their part; the **publication is admitted and idle** — no gate names
it, and no work reaches it while the transport runs. This is exactly the class the three rows were built to expose, and
it needed no new diagnostic to read.

## 3. What this episode does *not* settle (and why I stop here)

* **One episode, one shape.** The evidence establishes *this* outcome for *this* target; it does not establish a rate or
  a general law about the publication owner under transport.
* **The producer was reached, not proven inadequate.** Its selector chose the target and its gates admitted the turn, so
  on this evidence the restore-versus-rootless architecture question is **not** reopened: what is missing is *serviced
  work under transport*, and naming the mechanism behind that (turn scheduling, the publication's own unit pacing, or a
  queue that never receives the owed passes — the publisher's own rows show `q=0, a=0` where a queue count would be
  expected) is the next question, not this episode's answer.
* **F-7, F-8, AC-8 and task 6.5's four hardware claims remain NOT RUN** — the rolling demand was never opened on the
  target (step 5 was never reached), so nothing about the lane's service, timing or memory is measured.
* **No fallback, enablement or user-data change**: the lane stays default-off outside the qualification image, the
  legacy merge path is untouched, and only the authorized scratch slots (operator track 8 slots 5, 6) were written.

## 4. Captures

| capture | what it is |
|---|---|
| `captures/s64-etarget-02` | the preparation run: the even-length (18432) committed, demoted target |
| `captures/s64-trace-01/host_midi_hitl_long_loop_display_serial_…log` | the episode's leg half (transport, the leg's own record on the active slot, NOTE_EDIT phases) |
| `captures/s64-trace-01/host_midi_hitl_windowed_post_stop_drain_…log` | the switch's half: the queued switch, the three row families, the hold report, the post-stop publication units |

## 5. The bounded prerequisite this episode needed (F-17), and its host regression

The profile could not be driven through the runner's **managed** capture path: the runner appends
`--out-dir <dir>` (`hitl/runner.py`) and the managed hand-off appends `--follow-serial-log <session log>`
(`hitl/managed_capture.py`), and the reused leg's **strict** parser answered `unrecognized arguments: …` before a single
gesture (reproduced in `captures/s64-win-gate-01`). Fixed by **accepting** both in the leg
(`hitl/scenarios/long_loop_display_window.py`): a `FollowedSerialLog` read-only view (`.snapshot()`, no-op `.stop()`)
becomes the leg's line source when the capture owns the port, `--out-dir` becomes its artifact directory, and
`--follow-current-session` resolves through the canonical `.current_session` pointer. **Nothing is stripped**: the
wrapper still consumes only its own two flags, and a managed run that silently lost `--follow-serial-log` would have no
capture at all.

Host regression (no device): `scripts/test_windowed_content_parity_verify.py::ManagedRunnerHandshakeTests` — the
wrapper's flag consumption, the leg's acceptance of the exact managed argv, the adapter's collector interface, and the
canonical-pointer resolution. Suite: **27/27**. Neighbouring host suites re-run green
(`test_long_loop_display_serial_verify`, `test_managed_capture` — 19/19 together).

**Recorded, not patched (the remainder of F-17):** the wrapper's *post-leg* phase (`run_post_leg_phase`) still requires
the run to own a serial port — it returns early without pressing the transport stop when the managed capture owns it —
so the switch leg and the drain still run in the `--serial-port` shape, which is what this episode used. That is a
second, separate prerequisite and is left for the leg's owner.
