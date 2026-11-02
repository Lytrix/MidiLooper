# C5.3b — device lane cadence at 300 BPM in three states: Record, Overdub, Play (DEC-246 amendment 25)

Date: 2026-11-02. The owner's "most intense setup": every track with content playing, plus the HITL record on track 8
slot 4, at 300 BPM. Then an overdub phase and a play phase, so all three states are measured.

## Setup

- **Image.** Measurement env `teensy41-capture-serial-revstore-succprobe` (never shipped), HEX `34472436ac4cd162…`.
  - The reader image plus the successor-lead probe, enabled at boot.
  - Per-track `lane_fair` and `succ` rows.
  - `win_gap` schema 4.
  - In this image only, the three lane rows are written directly to USB when there is room (the ring otherwise).
  - RAM1 is identical to the reader image (ITCM 392,280 + 936, variables 96,224 / locals 34,848); the probe state is
    about 3 KB of RAM2.
- **Boot.** `#CAP,2163003,DIAG,boot_reset,crash=0`, then `BOOT,load,ok`; no crash in the run.
- **Stimulus.** `STIM-LANE-CADENCE-TEMP-01` (`scripts/hitl/scenarios/lane_cadence.py`, intense mode), via
  `host_midi_hitl.py run --scenarios lane_cadence --track 8 --loop-slot 4 --midi-channel 1 --boot-reset`.
  1. Transport stopped and restores settled: select track 8, clear slot 4 (the authorized HITL slot), and arm the
     record. It is armed while stopped because a record start needs its slot's record storage prewarmed, and that
     stepped owner runs only while stopped (DEC-231 / DEC-243). An arm under a running set never starts.
  2. The host clock starts at 300 BPM. The armed take starts at tick 0, and every track starts playing
     (`ST,Track,STOPPED,PLAYING` x 8).
  3. **Record:** 32 bars of 16ths (512 notes), stopped on the grid from inside the stream.
  4. **Overdub:** a double press on slot 4, then 200 bars of 16ths (3,200 notes, about 166 s).
  5. **Play:** 200 s.
  - Each step is confirmed from the device's live logger lines. Tier-A rows are retained in the ring while the
    transport runs, so their position in the file is not their time.
- **Capture.** `captures/c5-3b-lane-intense-07/session_20261102_122541.log`. The per-phase summary is
  `lane-cadence-phases.json` (phase boundaries from the device's live lines and the scenario's marks).
- **The card's set** (owner: no other recording). Content on track 1 (1 bar), track 7 (16 bars) and track 8 (the HITL
  take); the other tracks are empty.

## Result (300 BPM = 960 ticks/s; a two-bar span is 1,536 ticks)

| Phase | Device time | Seams | `win_gap` blocks | 16 turns: p50 / p99 / max (ticks) | 128 turns: p50 / p99 / max (ticks) | 128 turns, worst |
|---|---|---|---|---|---|---|
| Record | 18.1–44.3 s | 16 | 727 | 216 / 256 / **416** | 1,640 / 1,904 / **1,920** | 2.00 s (about 15.6 ms per turn) |
| Overdub | 48.4–214.3 s | 104 | 10 | 280 / 456 / **456** | 2,104 / 2,304 / **2,304** | 2.40 s (about 18.7 ms per turn) |
| Play | 214.3–414.1 s | 125 | 1 | (the lane is idle) | (the lane is idle) | — |

- **The worst gap after an eligible turn:** 48 ticks in Record, 112 ticks (117 ms) in Overdub.
- **The worst worker span:** 502 µs in Record, 567 µs in Overdub.

**Reading.**
- **A dense successor's own work (14–16 turns)** takes at most 416–456 ticks, under a third of a span, even at
  300 BPM during record or overdub.
- **A successor under worst arbitration (112–128 turns)** does **not** fit one span at 300 BPM during an overdub: 128
  turns took up to 2,304 ticks against a 1,536-tick span. During record it was 1,920.
- In time terms, the worst turn under this load is about 16–19 ms averaged over 128 turns. The single worst gap after
  an eligible turn was 117 ms.

## What this run could not measure, and why

- **Eight-track lane arbitration (`lane_fair`):** 0 rows, so no track was ever kept waiting while another was served.
- **Successor request → ready on the device (`succ`):** 0 rows; the probe never had a served window to follow.
- **Why both are empty: restored slots are not served by the window lane** (the DEC-243 / `playback-tick-consume-only`
  task 5.0 gap).
  - The card's tracks restore without a lane view set, and their producer exits `win_track_not_playing` for the run.
  - Over the whole run the tick routes are `view` 4,579 against `merged` 132,130: the set plays from the merged
    fallback.
  - So only the freshly recorded HITL slot ever uses the lane, and there is no multi-track lane pressure for a
    successor or a denial streak to show.
- **Play phase:** with no lane work the lane is barely dispatched, so its spans measure idle time, not arbitration.
  Its 21,848-tick "eligible" gap is the gap after the last grain of finished work, not a starved holder.
- **Overdub sample is thin:** only 10 blocks reached the capture.

## Production findings in the same run (not this slice's to fix)

- **1,348 `saveNoteEditPass rejected: heap below reserve`** (need 32,808 B, free 12–24 KB) during the dense overdub:
  edit and companion passes are dropped under load. This bears on the overdub-companion investigation.
- An earlier run of the same scenario (`c5-3b-lane-intense-05`) also showed `Capture admission rejected: unmatched
  NoteOff` (44) and `capture hot stop verify: non-canonical storage` at overdub stops.

## Decision inputs (amendment 25)

- **Content cost is cheap on the device too:** a dense window's 14–16 turns fit easily, even at 300 BPM under
  capture.
- **The worst turn under this load is about 16–19 ms per turn** (over 128 turns), with single gaps up to 117 ms.
- **The arbitration parameter (denied turns) cannot be calibrated on this card until restored slots are
  lane-served** (task 5.0, or the manifest reader's restored-slot path).
- **Arithmetic from what was measured:** 128 turns x 18.7 ms = 2.4 s. A two-bar span lasts 480 / BPM seconds, so it
  holds that only below about 200 BPM. At 300 BPM, eight-way arbitration would need a deeper lookahead or a cheaper
  turn.

## Gates

- **FAST** (capture ring, Tier-A tier, lane measurement): 75/75.
- **FULL** after the probe's device split: 3,933 cases, 8 skipped, 3,925 passed. The delivery change after it applies
  only to the measurement image (behind its build flag); native compiles none of it.
