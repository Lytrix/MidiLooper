# 3.15f — device leg, session 2 (2026-10-23): C images and the NOTE_EDIT seed blocker

**Change:** `windowed-playback-generation` · **Task:** 3.15f · **Branch:** `feature/unified-rolling-playback-path` ·
**Commit:** `5537df1c` · **Status: device leg started; blocked on the sparse NOTE_EDIT seed, not on C.**

## Images

| Image | HEX SHA-256 | Built |
|---|---|---|
| default | `561b6feb5b86fbbda6ff3428f21237f5aa6b8ebb871055d7c31e975dab757497` | 10:34 |
| qualification | `e1c07416f1d18ae91c2d4b88b53db697e307853fb80e0089c1d4aaee344ac316` | 10:35 |

Both carry the C contract (absolute future handoff positioning, ready-carried position, timed adoption). Upload:
Homebrew `teensy_loader_cli 2.3`, qualification HEX, `--mcu=TEENSY41 -w -v`; full programming pass and `Booting`;
boot verified by USB enumeration (`16C0:0489`, `Teensy MIDI`). The auto path was the Teensy core's 134-baud serial
request; no GUI was running.

## What ran on the qualification image

1. `edit_record_prelude` (host clock 120, no boot-reset) → exit 0, and it seeds the **dense chromatic** 2-bar grid
   (44 notes, every 16th step occupied).
2. The minimal NOTE_EDIT probe (`scripts/hitl_note_edit_min_probe.py`) entered NOTE_EDIT correctly, but the intended
   select/move oracle does not fit this seed:
   - step 5 / pitch 64 is absent (dense grid);
   - step 18 (pitch 66) selects but the coarse move to step 20 is blocked (target occupied);
   - step 18 → 16 does not move (the midpoint pitchbend is a zero-delta fader move);
   - step 18 → 6 does not move (the dense layout's overlap/policy; no `POSITION EDIT` result in the content).
   The probe is written for the **sparse 8-note seed** used on 2026-10-22 (`49/60, 240/64, 432/67, ...`).
3. The combined `edit_record_prelude,note_edit_reselect_stacked` run exits 1 in the reselect with the dense seed (it
   needs an **empty sixteenth**, and the dense grid has none), so it does not produce the sparse seed either.

## The blocker

The sparse NOTE_EDIT seed is currently produced only by the earlier 8-note path; the standalone prelude now records the
dense base, and the reselect preset cannot run on it. This is a **fixture/seed** gap, not a C defect:

- the native C contract is complete and green (see below);
- the device probe entered NOTE_EDIT and drove the session, but its fixed steps/pitch do not match the dense seed, and
  the dense seed has no empty target for a simple position move.

**Next:** record the sparse seed directly with a minimal seed probe (clear the scratch slot, arm, stream the known
8-note pattern at 120 BPM, stop), or extend the minimal probe to select a dense-grid note with an **empty** target step
and prove the move. Then run the NOTE_EDIT probe on the qualification and default images and compare the post-move rows
(as in `2026-10-22-s315f-default/lane-noteedit-probe-0x`).

## Native C contract (for the device comparison)

- Occurrence search + absolute deadline; ready-carried position; every adoption route guarded; build-worker discard
  preserves the ready; the hot path's staleness reset is skipped while a positioned preview ready is owed.
- Timed successor adoption: waits through H−1, survives the staleness turn, adopts at H.
- Measured two-bar NOTE_EDIT latency: **2,972 ticks** = 1.94 loop periods = **~7.7 s at 120 BPM**.
- Gates: preview 36/36; full native 3,420 / 7 skipped / 3,413 succeeded; images `391,752 + 1,464` / `391,992 + 1,224`;
  runtime pins 7,920 / 10,120.

**Board left on:** qualification image `e1c07416…`, dense-seed content on the scratch slot.
