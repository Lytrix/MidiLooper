# 3.15f — device leg, session 1 (2026-10-22): flash, boot and a 120 BPM parity pair

**Change:** `windowed-playback-generation` · **Task:** 3.15f · **Branch:** `feature/unified-rolling-playback-path` ·
**Commit:** `8e086e39` (tree dirty only by untracked `.current_session*`/`metadata.json`) · **Status: 3.15f stays open.**
This records what the device leg established and what it did not.

## Images and flashing

| Image | HEX SHA-256 | Built |
|---|---|---|
| default (`teensy41-capture-serial`) | `04ea9a8a55a32c87d66d9393d3dcbae4b4fc408223cea6f215b3abdc4f3b1d2f` | 22:51 |
| qualification (`teensy41-capture-serial-window-qualification`) | `dfbcfa1c92c48eecd55ad8d5c5ec6afed4a53365f65d693ee95d7469de5ae27b` | 22:42 |

Uploader: Homebrew `teensy_loader_cli 2.3`, `--mcu=TEENSY41 -w -v`, each write reported a full programming pass and
`Booting`; boot verified separately by USB enumeration (`16C0:0489`, `Teensy MIDI`) and the capture's own
`host-requested reboot` / `boot_reset,crash=0` / `BOOT,load,start` rows.

**Automatic reboot works, but not via the loader's `-s`.** The Teensy core's 134-baud serial-open request puts the board
in HalfKay, so the waiting CLI programs without a button. The first two writes used it successfully.

**A Teensy Loader GUI in Auto was the cause of two aborted writes.** With the GUI running, the CLI write ended
`error writing to Teensy` and the board dropped off USB; the guide's "one uploader at a time / disable Auto" is the rule
that was being violated. After quitting the GUI and one USB replug, the same checked HEX wrote cleanly. A failed write is
not a boot failure and was not treated as one.

## The tempo finding (owner-observed)

The device was running **90 BPM** internally while the stimulus expects **120**. The first two lane runs were made in that
state. Passing `--tempo-bpm 120 --clock-source host` (the runner forwards these to the baseline) makes the host the clock
master at 120; the content then plays with a 4 s two-bar period, confirmed in the capture.

## Base stimulus runs (STIM-BASE-2-2-01), track 5 / slot 1

| Run | Image / clock | Result | `max_pers_work_us` | `max_persist_save_us` | Active snapshots |
|---|---|---|---|---|---|
| `2026-10-22-s315f-default-base-01` | default / device 90 | **PASS** | 38,674 | 43,445 | 10 / 10 |
| `2026-10-22-s315f-lane-base-03` | lane / device 90 | INCONCLUSIVE | 100,406 | 106,482 | 7 / 8 |
| `2026-10-22-s315f-lane-base-04` | lane / device 90 | FAIL (record) | 38,076 | 38,540 | 8 / 8 |
| `2026-10-22-s315f-lane-base-05` | lane / device 90 | **PASS** | 66,205 | 66,610 | 8 / 8 |
| `2026-10-22-s315f-default-base-02-hostclock` | default / host 120 | **PASS** | 39,007 | 39,409 | 7 / 7 |
| `2026-10-22-s315f-lane-base-07-hostclock` | lane / host 120 | **PASS** | 37,218 | 37,624 | 9 / 9 |
| `2026-10-22-s315f-lane-base-08-ch5` | lane / host 120, ch 5 | **PASS** | 38,199 | 43,052 | 8 / 8 |
| `2026-10-22-s315f-default-base-03-ch5` | default / host 120, ch 5 | **PASS** | 51,413 | 51,798 | 8 / 8 |

The declared deferred-persistence bound is **100,000 µs** (`_MAX_PERSIST_SLICE_US`). The two 90 BPM lane anomalies:

- **Persistence overrun once** (100,406 / 106,482, one active-snapshot window missed). Not reproduced at 90 BPM on the
  next run (38 / 38) or at 120 BPM (37 / 37).
- **Record-stream stall once**: 32 On/Off arrived, but 17 deltas were off-grid (13–48 ticks vs the 48-tick grid) and the
  raw span collapsed to 1,230 (minimum 1,339). lane1 and the default at the same tempo were `1,783` / `1,799` with a
  perfect grid. This is a host-stream/MIDI delivery stall, not a stored-content fault.

**At the requested 120 BPM with the host as clock master, default and lane both PASS and are comparable** (39.0/39.4 vs
37.2/37.6 ms). That is the route-parity result this session can support for the base stimulus.

## Content pipeline: the channel-5 run gives exact cycle parity

The first pipeline attempts returned `INCONCLUSIVE_CAPTURE` ("fewer than one loop's 168 events") on **both** images. Two
setup causes, both measured:

1. **Channel mapping.** The base stimulus recorded with `--midi-channel 1`, but the track outputs on its own channel
   (track 5 → 0-based channel 4). The capture therefore never contained the expected channel.
2. **Other restored-library traffic.** Before the channel-5 run the capture also carried other restored tracks' channels,
   so the single-loop comparison could not form. This is **not** a lane-parity failure; the SD was not cleared (user data),
   and a solo long-press on the track button (double = mute, long = solo) did not remove the control traffic.

With `--midi-channel 5` (the track's own channel) and host clock 120, both images form cycles and agree **exactly**:

| Image | Cycles | Rows / cycle | Missing | Extra | `inventory_equal` |
|---|---|---|---|---|---|
| lane (`…lane-base-08-playback`) | 3 | 192 | 0 | 24 × CC74 | false |
| default (`…default-base-03-playback`) | 3 | 192 | 0 | 24 × CC74 | false |

The 24 extras are **identical on both images**: CC 74 values 1/2 at the same phases, i.e. the rig's fader/automation
traffic on the same port, not loop content (the dump itself carries 56 CC74 effective rows; the extras are the
control-side echo). `inventory_equal` is false only because of them. The lane run's leading fragment also showed two
`NoteOff` rows (`[128,5,31,0]`, `[128,5,19,0]`) outside the three cut cycles; the per-cycle comparison is missing 0.

**Conclusion this session supports:** for the canonical base stimulus at 120 BPM with the host as clock master, the
served loop content (all note rows, order, per-cycle counts) is identical between the default and qualification images.
The verifier's strict inventory equality is blocked by rig CC74 traffic, not by the lane. `storage_matches_effective` is
true on both.


## NOTE_EDIT pair attempt (`edit_full`): not yet a usable baseline

Two default-image attempts, both with the canonical `edit_full` preset (2 bars, host `--tempo-bpm 120`, track 5 /
slot 1 / channel 5):

1. **At the device's internal 90 BPM** (`…default-editfull-02`, boot-reset): the record prelude's live display check
   failed (`live_record_display:reca_missing`) and the edit suite reported ~30 markers, all note-edit gestures.
2. **After setting the loop-edit tempo fader to 120** (pitchbend on the tempo channel: firmware log
   `TEMPO: PB=-1672 -> 120.0 BPM`, verified in LOOP_EDIT), run **without** boot-reset so the RAM tempo survives
   (`…default-editfull-03-tempo120`): the record-display failure is gone, but the note-edit markers still fail
   (`POSITION EDIT`, delete/note-value/move/length/overlap/undo markers all absent).

So the tempo correction fixed the record leg, and the remaining gap is the **edit-session/fader precondition**
(entering the edit session, selecting a note and the fader bank) on this bench — not a lane/default difference. The lane
leg was **not** run against a failing default baseline; doing so would not be a parity test. The `edit_full` pair is
therefore **not yet a parity result**. **The fixture, not the edit entry, is the blocker.** The run log shows the session entering correctly
(`Edit session: NOTE_EDIT (Program 1, Note 0 trigger)`, select-fader routing, `EditSelectNoteState: Found and selected
note 1 at tick 240`), so the precondition is not note-edit entry. Comparing the last archived `edit_full` run
(`captures/host_midi_automation_edit_baseline_20260804_174049.json`, same track 5 / channel 5): it has the **same** core
markers missing (`m0_moved_events_low:0`, `m0_missing_before_length_edit`, `POSITION EDIT`, `Note value …`) and also
`capture:ring_overflow`. The legacy `edit_full` fixture is therefore **chronically failing on this bench**, not newly
broken by this tree; its missing markers are not evidence about the lane and it is **not a usable 3.15f oracle**.

The layered `note_edit_overdub_compose` preset was also tried on the default image
(`…default-necompose-01`, 120 BPM, no boot-reset) and failed at its own precondition: `no later same-pitch live note`.
Archived `note_edit_overdub_compose` reports are **also all `ok=False`** in the recent history
(`captures/host_midi_hitl_note_edit_overdub_compose_2026-09-10*`), so this is not a one-off seed miss. **Both NOTE_EDIT
device oracles are stale on this bench.** This is a host-fixture/oracle gap, not an image or lane finding; qualifying the
NOTE_EDIT route needs a repaired or new device oracle (a host-tooling task), or the NOTE_EDIT term is explicitly deferred
with this named gap. The lane was not run.

The tempo-fader route is now a reusable device recipe: enter LOOP_EDIT, send pitchwheel on the tempo channel with the
`TempoMapping` logical value (120 BPM → wire 6520, logical −1672); the firmware answers `TEMPO: PB=-1672 -> 120.0 BPM`.
Do not boot-reset after setting it, or the saved 90 BPM is restored.

## Not claimed

- 3.15f is **not passed**: the base stimulus's served-cycle parity is established, but the full registered content
  profile (all routes, simultaneous Record, NOTE_EDIT, switch, undo/redo, persistence/display contention, RAM1/PSRAM
  high-water) is not, and the short-loop window-route mechanism is still open.
- The 90 BPM overruns are not called firmware regressions: they did not reproduce at the requested 120 BPM, and the
  record failure is a host-stream stall.
- No claim about the 3.15e seam on hardware; this session exercised the base stimulus only.

## Next session (ordered)

1. Establish an isolated content target: mute/quarantine the other stored tracks with owner permission, or use a device
   library prepared for qualification, so the playback capture is the target loop alone.
2. Re-run the base stimulus and the content pipeline on both images at 120 BPM external clock; compare inventories and
   per-bar counts, then the remaining 3.15f routes (simultaneous Record, NOTE_EDIT, switch, undo/redo, persistence/display
   contention, RAM1/PSRAM high-water).
3. Decide the short-loop window-route mechanism before claiming route identity for short loops.

**Board left on:** default image `04ea9a8a…` (the final channel-5 default run), with the **RAM tempo set to 120**; the saved workspace BPM remains 90, so a reboot restores 90.
