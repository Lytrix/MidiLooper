# 6.4/6.5 — the first authorized device run of the windowed leg

Owner authorization for the upload was given in this session. Everything below is measured on the attached board; no
firmware line was changed, and nothing on the card was destroyed.

## What was run

| Step | Command | Result |
|---|---|---|
| Build | `pio run -e teensy41-capture-serial` | SUCCESS, RAM1 unchanged (`variables 96,192` / `code 391,576` + `padding 1,640`, floor 840) |
| Flash | `pio run -e teensy41-capture-serial -t upload` | SUCCESS; image `firmware.hex sha256 1337956a05c4aeda…` from git `abdbca63` |
| Pre-run state (read-only) | `scripts/hitl_boot_census.py --library-observe` | post-restore `free_chunks=404`, `admission/interactive/undo/stopped=1`, observation complete over 64/64 slots |
| Target selection (benign) | two selection presses (track 8, slot 6) | `Selected track 8`, `Button press: Loop 6 (short)`, `length=0 -> CC=0 (1 bar default)` — slot 6 empty |
| The leg | `host_midi_hitl.py run --preset windowed_content_parity --track 8 --loop-slot 6 --midi-channel 8 --serial-port …` | drove the device; capture at `captures/s64-window-leg-01/` |

**Safety, from the device's own rows.** Before the run, track 8's census showed content on **slots 4 and 5** (a 2-bar,
3-pass take; a 24-bar, 1 144-event take) and none on slots 6, 7, 8. The run therefore targeted **slot 6**, and the device
confirms nothing was removed: `MIDI: Clear removed persisted slot 6 payload (absent)`. Slots 4 and 5 were never named by
any row in the run, and `!QUARANTINE_WORKSPACE` was not needed because nothing was deleted.

## What the device did

It did the leg's work: `Loop 6: Start Recording` → `Recording stopped @ tick 348 (recStart=0 length=18432)` — a **24-bar**
take on the windowed profile's target shape — then NOTE_EDIT, the play/stop long-press and the hold.

## What the windowed lane produced

From `device-lane-rows.txt` (the trimmed extract; full capture in `captures/s64-window-leg-01/`):

- **44 `win_gap` blocks**, every one with `running=0`, `permitted=64`, schema `3`, and **zero** eligible/progress/denied
  turns; opportunities rising 11 776 → 22 784 across the idle phase, then stopping at 210.59 s — immediately before the
  record's transport start (211.50 s) — with **no block at all** for the remaining ~97 s of recording, playing and
  editing.
- **Zero `win_readiness` rows** in the entire capture.
- **Zero `win_gap_overrun` rows** (nothing exceeded the permitted advance — the one check that passes).
- **One `playback_build` row**, path `snapshot` on track 2 slot 0 — the legacy path, not this leg's slot.

The corrected verifier's verdict on that capture:

```
window_demand_observed: no eligible/progress turn and no readiness receipt in the whole capture: no slot owed a window
  build, so this run did not exercise the lane (a demand needs the rolling lane enabled, or a launch/retry in flight --
  a plain record-and-play owes none)
readiness_receipts_under_transport: receipts=0 under_transport=0
windowed_service_coverage_under_transport: blocks=44 under_transport=0 with_opportunities=0
window_boundaries_crossed_under_transport: build_complete_under_transport=0
no_windowed_overrun_under_transport: ok
```

## What this run establishes, and what it corrects

**It validates the row reading on real rows.** The field map the pin forced me to fix is right: a device block reads
`#CAP,116902617,DIAG,win_gap,0,11776,0,0,0,0,0,0,0,0,1,0,64,0,0,3` — `permitted` in field 16 (`64`), `overruns` in 17,
the literal schema `3` in 19, `running` in 7. The parser is no longer a synthetic-rows-only claim.

**It corrects the oracle.** The profile's checks presupposed that a long-loop leg *asks* the lane for window work. On
this image it does not, and the reason is the configuration, not a lane defect: `PLAYBACK_WINDOW_ROLLING_ENABLED`
defaults to **0**, so only the launch/retry work can owe a window, and a plain record-and-play owes none. With nothing
owed, a dispatch has zero eligible turns and emits no receipt — and the lane's rows are event-shaped, so "no work" and
"no rows" are the same observation. The verifier now says exactly that, as its **first** named check
(`window_demand_observed`), so nobody reads three lane failures as three lane defects. Red-proofed by **MR-6.4e**
(the prerequisite never checked) failing precisely the new test, file restored byte-identically
(`sha256 c66f1f0afcf5e5d5…`).

**Measured, and worth a decision rather than a fix (F-12).** The lane's dispatch stopped entirely once the transport
started: 44 blocks in the idle phase, none across ~97 s of recording/playing. The consistent explanation is that
`PlaybackViewMaintenance` is a **deferred idle owner**: with no slot owing window work, the arbitration has nothing to
serve there, and its sampler is only reached when it is chosen. That hypothesis is recorded as a hypothesis — what is
*measured* is the absence of blocks. It matters for 3.10 because F-2's deadline is 16–31 ms: if the lane is only
dispatched when nothing else is pending, a playing session cannot meet that deadline, and the native fixtures never
noticed because they drive `stepPlaybackViewMaintenance` explicitly.

**Recorded, out of this slice's scope (F-11).** The reused long-loop leg's own display oracle failed
(`missing_bounded_window_disp_rows`): the device emits `#CAP,190595715,DISP,5,RECORDING,1,0,0,0,0,0,0,1,0` (12 fields)
while that verifier parses the 14-field windowed DISP shape. The staleness is in that leg's oracle against the emitted
schema — it belongs to the display-window leg, not here, and since the profile's verdict is the AND of both halves, it
also means this composition cannot go green until that is reconciled.

## Not claimed

- The windowed lane was **not exercised**: no receipt, no publication, no coverage under transport. F-7, F-8 and AC-8
  remain unverified on hardware.
- No firmware change, no oracle change to fit an observation, no device claim beyond the rows quoted above.
- 6.4 and 6.5 both stay **open**.

## The decision this run hands back

The lane's device evidence needs a **window demand**, and on the current image there are exactly two ways to have one:

1. **Enable the rolling lane** (`PLAYBACK_WINDOW_ROLLING_ENABLED=1`) — task 3.10's activation, which is the owner's
   enablement decision, not a verification step. With it, the leg as written would carry the demand its oracle expects.
2. **Exercise a demand that exists today**: a **demoted slot switch** (a switch to a target far longer than the served
   window) makes the *launch* prepare a window, which is the path the native fixtures already prove — that is a change
   to the leg's gestures (its driver), not to the firmware.

Both are decisions for the owner; this run does not take either.

---

## Correction, same session — the qualification image, and what the absence of `win_gap` rows actually means

The owner chose the enablement decision, so the lane was opened **in a dedicated qualification build** rather than in the
shared default, which is what the firmware's own guard demands (`include/RuntimePlaybackView.h`: rolling needs native, or
`SESSION_CAPTURE` **and** `PLAYBACK_WINDOW_ROLLING_QUALIFICATION`). The new env
`teensy41-capture-serial-window-qualification` extends the capture env with `-D PLAYBACK_WINDOW_ROLLING_ENABLED=1
-D PLAYBACK_WINDOW_ROLLING_QUALIFICATION=1`, and the RAM1 guard refused it until a baseline was recorded **deliberately**
— measured on the linked image: `variables 96,192`, ITCM `393,216` (`code 392,232` + `padding 984`), locals `34,880`, i.e.
**+656 B of ITCM code** for the rolling path, padding still 144 B above the 840 floor, banks and RAM1 variables unchanged.
The entry and its reason are in `scripts/ram1_budget.json`. The image was built and flashed
(`firmware.hex sha256 597ab7675a83d7cc…`), and the leg was driven again (`captures/s64-window-leg-02/`).

**The second run reproduced the first run's row picture, and that is what forced this correction.** It produced 13
`win_gap` blocks (all `running=0`, `permitted=64`, schema `3`, zero eligible/progress turns) whose last block sits at
`#CAP,42751221,…` — immediately before the record's transport start at 43.2 s — and then **exactly one** `#CAP` row for
the remaining ~262 s of the run (a `PERS,result` from the *immediate* path). F-12's earlier framing, *"the lane's dispatch
stopped entirely once the transport started"*, is **withdrawn**: it read a capture-regime property as a dispatch
property, and the code says which regime it is.

**What the code says.** `DebugSessionCapture::flushCaptureBuffer` sheds **non-Tier-A** head records when the budget is the
timing-critical 8 (`while … if (headRecordIsTierAText()) break; discardOldestRecord(TimingShed)`), and `main.cpp` flushes
with `SC_CAPTURE_FLUSH(timingCriticalTrackActive ? 8 : 64)` while a track plays. `include/Utils/CaptureLineTier.h` lists
`DIAG,playback_build,`, `DIAG,playback_order,` and `DIAG,win_readiness,` as Tier-A — and **not** `DIAG,win_gap,` or
`DIAG,win_gap_overrun,`. So under transport the lane's **coverage and overrun rows are shed silently by design**, which is
exactly the shape this capture shows: the instrument went blind for the whole playing session, and every `win_gap` block we
can see is from the idle phase.

**F-13 (new, actionable, and the reason 6.5's hardware claims cannot be measured yet).** The lane's declared
"operationally reported" permitted-advance envelope is carried by `win_gap`/`win_gap_overrun` — the two families that are
**not** Tier-A — so the rows that would measure the envelope are the ones dropped in the regime they exist for. This is a
deliberate firmware change (a tier entry, with its own boundary fixture), not a verification step, so it is recorded here
and **not** made in this slice: the authorization for this path was the lane's **enablement**, and an instrument change is
a separate decision.

**What survives unchanged as device evidence.** `win_readiness` is Tier-A **and** has the direct-when-USB-has-room
delivery, so its complete absence in both runs is evidence rather than a blind spot: **no readiness receipt was produced**
— consistent with "no window work was owed during the observable phases" (the idle phase cannot owe one: the rolling
request declines while a slot is not playing). F-11 stands exactly as recorded.

**Corrected summary of what the two device runs say.** With the lane off: no lane rows owed, nothing observable. With the
lane on: the same, plus the discovery that the instrument itself sheds the lane's coverage rows under transport. Neither
run exercises the lane, so F-7, F-8 and AC-8 remain unverified on hardware — and the next step is no longer only the
enablement decision but F-13's tier entry, without which the envelope cannot be observed at all.
