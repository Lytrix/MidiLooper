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

**Further dense-grid attempts (same session, all negative):**

- step 18 -> 20 (occupied target): no move;
- 18 -> 16 (midpoint): zero-delta fader, no move;
- 18 -> 6: no move;
- 18 -> 14 (empty target): the dump shows a **different** note moved (pitch 79, 1488 -> 336); the step-based select
  mapping picks a nav slot, and the dense grid's off-grid/stacked notes make that slot the wrong note. The probe's
  one-note-per-step assumption is invalid here.
- `edit_record_prelude --edit-record-fixture` (host clock 120) exits 0 but the slot content is still the dense grid; the
  seed's clear/record does not replace it.

**Sparse seed found (same session).** The prelude scenario is a no-op outside `edit_full`; the sparse seed is the
**edit fixture**, produced by `host_midi_hitl.py run --preset base --record-only --edit-record-fixture ...` (exit 0,
PASS). It records 8 notes at `8/60, 200/64, 392/67, 584/60, 872/64, 1064/65, 1256/69, 1352/55` -- the fixture phase is
**+8 ticks off the 48-tick step grid** (the earlier 2026-10-22 seed was on-grid at 49/240/432...).

**Two remaining probe issues (device state, not C):**

1. **The step-based select does not reliably focus the off-grid note.** Selecting step 4 (the step containing the note
   at 200) and moving to step 6 moved **note 55** (1352 -> 344), not pitch 64; the edit session's **focus persisted
   from the previous probe run**, so the coarse fader applied to the stale focus.
2. The seed phase (+8) is outside the step mapping the probe uses for the source note.

**Next (host tooling):** the probe must establish a **fresh edit session/focus** per run (exit then enter NOTE_EDIT, or
start from a known unfocused state) and select by the seed's **actual nav slot** (or align the record phase so the seed
is on-grid). Then the lane/default NOTE_EDIT pair can run. This is not a C change. Board left on the qualification image
with the sparse seed.

## Native C contract (for the device comparison)

- Occurrence search + absolute deadline; ready-carried position; every adoption route guarded; build-worker discard
  preserves the ready; the hot path's staleness reset is skipped while a positioned preview ready is owed.
- Timed successor adoption: waits through H−1, survives the staleness turn, adopts at H.
- Measured two-bar NOTE_EDIT latency: **2,972 ticks** = 1.94 loop periods = **~7.7 s at 120 BPM**.
- Gates: preview 36/36; full native 3,420 / 7 skipped / 3,413 succeeded; images `391,752 + 1,464` / `391,992 + 1,224`;
  runtime pins 7,920 / 10,120.

**Board left on:** qualification image `e1c07416…`, dense-seed content on the scratch slot.
