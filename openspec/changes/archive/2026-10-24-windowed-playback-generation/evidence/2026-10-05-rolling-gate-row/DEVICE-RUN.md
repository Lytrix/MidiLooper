# The device read: the demand's exit, and three runs that fixed the row's cadence

Authorized device work on 2026-10-05 (owner: *"Authorize the upload + windowed leg now"*), by the **registered** profile
`STIM-WINDOW-CONTENT-01@v1` (`windowed_content_parity`) through the existing content-verification pipeline. No canonical
stimulus was edited; the leg's own recipe (`--record-bars 24`) and its verifier are the change's registered ones.

| run | capture | image (firmware.hex sha256) | what it is |
|---|---|---|---|
| 03 | `captures/s64-win-gate-03` | `97f67caf868a3939…` (git `c280555b`) | the first reading, with **every** bit in the row's change key |
| 04 | `captures/s64-win-gate-04` | `3cc5068d121c5ede…` | after the **first** key correction (bits 11 + 13) — the wrong bits |
| 05 | `captures/s64-win-gate-05` | `cd2ca8c52e53d7fc…` | after the corrected key (bits 10/11/13/14) |

Invocation (each run, after a flash of `teensy41-capture-serial-window-qualification` and a port wait):

```
.venv/bin/python scripts/host_midi_hitl.py run --preset windowed_content_parity \
  --track 8 --loop-slot 5 --midi-channel 8 \
  --serial-port /dev/cu.usbmodem154944801 --out-dir captures/s64-win-gate-0N
```

## 1. The answer to the question five runs could not settle

The driven track's own rows (`t=7`, the leg's 24-bar record target = operator track 8 slot 5) in run 05, on every turn
where the track was **playing**:

```
x=win_window_ineligible, len=19200, ep=1, av=1, al=0, pl=1   <- the demand's entry decision
x=win_track_not_playing, len=19200, ep=1, pl=1, ed=1         <- a note-edit session is open
x=win_track_not_playing, len=19200, ep=1, pl=1, od=1, rw=1   <- a deferred overdub stop + record-return window
```

* **The clock stamp was never the problem.** `ep=1` in *every* row: `clockManager.getPlaybackDeadlineClock()` is stamped
  under this transport, so `!clock.epoch` (the hypothesis the earlier record carried as the leading candidate) is
  **refuted by the row**.
* **The entry term that refuses is span alignment** (`al=0`): the loop the leg's own record stop produced is
  **19200 ticks = 25 bars**, and 19200 % 1536 = 768 ≠ 0, so it is not a whole number of two-bar spans — while it *is*
  demoted (`av=1`, > 16 bars). A demoted, unaligned, playing loop cannot take a windowed interval by construction.
* The other refusals are correct and now visible: an open note-edit session (`ed=1`) and a live deferred overdub stop
  plus its record-return window (`od=1, rw=1`) — both states the lane must refuse in, and both previously inferred.
* The **second** gate is named too, on the tracks whose demoted loop *is* aligned: `x=win_not_servable` with `rt=0`
  (`publishedSnapshotIsCurrent()` false) — the "rootless demoted slot" tension of
  `evidence/2026-10-05-switch-leg-demand/MEASUREMENT.md`, now measured as a row rather than deduced.

**The consequence for task 6.4/6.5, stated plainly:** this leg shape **cannot** produce a window demand at all — not
because the lane is broken and not because the clock is missing, but because the loop it commits is not span-aligned
(25 bars) and, where it is, its root is not current. The next leg needs a committed length that is a whole number of
two-bar spans (24 bars = 18432 exactly, or 26/28), which is a **leg-shape** decision, and the root question is the one
already recorded as an owner decision (RWO's restore edge vs a DEC-108 source rule).

## 2. The row's cadence was wrong on device, and three runs fixed it

The row is emitted on a **state change**, so the change key decides whether it is readable or a flood. Device run 03
measured the flood:

| run | change key | `win_gate` rows in the leg capture | max repeats of an identical payload | fields differing between consecutive rows |
|---|---|---|---|---|
| 03 | every bit | **809** | **20** | `bld` 758/775, `rl` 757/775, `len` 26 |
| 04 | bits 11 + 13 excluded | **817** | 23 | `rl` 757/789, `bld` 756/789 — **the wrong bit** |
| 05 | bits 10/11/13/14 excluded | **57** | **2** | `len` 27, `al` 25 — genuine (the loop grows) |

* The mechanism is `docs/Agents/hitl_authoring_traps.md`'s **"dedupe that alternates forever"**: two states differing
  only in observation bits (`rl` = `coverageRoleAvailable`, `bld` = `viewBuild.active()`) alternated A↔B, so *every*
  transition was a genuine key change and the row re-emitted ~20 identical lines per length step.
* Run 04 is recorded because it is the **wrong first guess**: the header's bit list has `coverageRoleAvailable` at **10**
  and `ownsCoverageSource` at 11, and clearing 11 + 13 left `rl` alternating — the device said so, and the second fix
  followed the measurement rather than the assumption.
* The corrected key keeps the bits that are *reasons* (the entry trio 0–2, the not-playing terms 3–7, the not-servable
  pair 8–9, the demand-active input 12, re-projection 15) and excludes the four observation bits. A change in an
  excluded bit still reaches the reader: when the guard it belongs to clears, the **exit changes** and the transition is
  named by the new row.
* 14× fewer rows, no alternation, and the remaining differences are the loop actually growing (`len`, `al`) — i.e. the
  cadence now tracks the decision.

## 3. The profile's own oracle, replayed over the run

The **registered** verifier judged run 05's capture (verify-only replay, `captures/s64-win-gate-05-verify`):

```
lane: window_demand_observed                        ok=false  "no slot owed a window build, so this run did not exercise the lane"
lane: readiness_receipts_under_transport            ok=false  "receipts=0 under_transport=0"
lane: windowed_service_coverage_under_transport     ok=true   "blocks=14 under_transport=1 with_opportunities=1 permitted=64"
lane: no_windowed_overrun_under_transport           ok=true   "overrun_rows=0 under_transport=0"
lane: window_boundaries_crossed_under_transport     ok=false  "build_complete_under_transport=0"
leg:  missing_playing_bounded_window_disp (F-11, the reused leg's stale DISP oracle), + 2 related DISP counts
```

Read together with §1, the oracle's first line and the row say the **same** thing at different resolutions: the oracle
says no slot owed a window build; the row says **why** (unaligned demoted loop, and a non-current root where it is
aligned). That is precisely the distinction F-16 named.

## 4. What these runs do **not** establish

* **F-7, F-8 and AC-8 remain unverified**, and task 6.5's four hardware claims (grain duration and maximum service gaps,
  sustained multi-track playback with capture/persistence contention, external-clock behaviour, memory high-water marks
  with boundary content parity) are still **NOT RUN**: the lane was never given a demand in any of these runs, so
  nothing about its *service* was measured. **6.4 and 6.5 stay open; nothing is checked off.**
* **No fix of the lane, the leg or the content pipeline.** The only production line this device work changed is the
  row's own change key (its cadence), which is instrumentation; the leg's display-oracle issues are the **reused** leg's
  recorded F-11, not this change's.
* **A host-tooling gap is recorded, not patched here (F-17):** the profile cannot be driven through the runner's
  **managed** capture path — that path injects `--follow-serial-log <session log>` and `--out-dir <dir>` into the reused
  leg's arguments, and the leg's strict parser rejects both (`unrecognized arguments`), while the leg's own LOOP_EDIT
  precondition wants the managed path's boot banner. The runs above therefore use `--serial-port` (the leg owns the
  port), which is the shape the two earlier successful legs used. Both failures are reproduced exactly
  (`captures/s64-win-gate-01`, `-02`) and are recorded for the leg's owner.
