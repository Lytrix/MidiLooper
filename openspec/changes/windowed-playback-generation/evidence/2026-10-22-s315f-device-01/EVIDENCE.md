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

The declared deferred-persistence bound is **100,000 µs** (`_MAX_PERSIST_SLICE_US`). The two 90 BPM lane anomalies:

- **Persistence overrun once** (100,406 / 106,482, one active-snapshot window missed). Not reproduced at 90 BPM on the
  next run (38 / 38) or at 120 BPM (37 / 37).
- **Record-stream stall once**: 32 On/Off arrived, but 17 deltas were off-grid (13–48 ticks vs the 48-tick grid) and the
  raw span collapsed to 1,230 (minimum 1,339). lane1 and the default at the same tempo were `1,783` / `1,799` with a
  perfect grid. This is a host-stream/MIDI delivery stall, not a stored-content fault.

**At the requested 120 BPM with the host as clock master, default and lane both PASS and are comparable** (39.0/39.4 vs
37.2/37.6 ms). That is the route-parity result this session can support for the base stimulus.

## Content pipeline: inconclusive on both images, for a device-state reason

`dump_and_capture.py` + `verify_playback_content.py` returned `INCONCLUSIVE_CAPTURE` ("no unique effective event recurs …
fewer than one loop's 168 events") on the **default** image as well as the lane. The capture is not the target loop alone:
the played stream carries other channels from the board's restored library (e.g. channel 4 note 16 and channel 14 notes
40–46 beside the target). The verifier compares one loop's effective rows, so the single-loop comparison cannot form while
other tracks sound. This is **not** a lane-parity failure, and the device SD was not cleared or muted (SD is user data;
that needs explicit permission).

## Not claimed

- 3.15f is **not passed**: the registered content profile's playback parity is not established, and the short-loop
  window-route mechanism (3.15f's design choice among sub-interval / whole-loop / position-free) is still open.
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

**Board left on:** qualification image `dfbcfa1c…`.
