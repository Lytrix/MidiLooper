# Manual test: capture session for known bugs (Bucket 1 S1)

Goal: reproduce bugs **B1–B4** from [`docs/plans/stabilization_known_bugs_bugfix.md`](../plans/stabilization_known_bugs_bugfix.md) once, on an instrumented build that records fixtures. The capture file is the deliverable — it lets the bugs be debugged and regression-tested offline without you at the hardware.

## Setup (once)

1. Flash the instrumented build:

```bash
pio run -e teensy41-capture -t upload
```

2. Start recording serial output to a file (leave running for the whole session):

```bash
mkdir -p captures
pio device monitor -e teensy41-capture --quiet > captures/session_$(date +%Y%m%d_%H%M).log
```

3. Confirm the file contains a `#CAP,...,HDR,v1` line after boot. All fixture lines start with `#CAP,`; normal log output is interleaved and that is fine.

While playing, speak/say nothing into the file — but **note the wall-clock time** whenever you see a bug happen, so the moment can be found in the capture.

## B1 — Recording length rounding (adds a bar)

Expected behavior: length rounds to the **nearest** bar.

1. Select an empty track, internal clock, default BPM.
2. Record a loop and stop **clearly just after** a bar line (within roughly half a beat after it). Note whether the resulting loop length matches the bars you played, or has one extra/missing.
3. Clear, then record again and stop **clearly just before** a bar line.
4. Repeat both cases once more, this time starting the recording slightly **late** (press record just after the bar line) — the start side is under suspicion, not only the stop side.
5. Each attempt produces `RECA` (record start) and `RECS` (record stop, with raw and final length) lines in the capture. Just note per attempt: intended bars vs. what you got.

## B2 — External-clock BPM display settles slowly

1. Connect the external MIDI clock source, start it at a known BPM.
2. Note how long the displayed BPM takes to reach the source value.
3. Change the source BPM by a few BPM (e.g. 120 → 124); note how long the display takes to follow.
4. Stop and restart the external clock once.
5. The capture records every `BPM` computation (window value vs. smoothed value) — no extra care needed beyond noting the source BPM you set.

## B3 — Motorized fader feedback stops updating

1. Enter the mode where fader feedback is active (NOTE_EDIT, select notes so faders get driven).
2. Trigger fader updates repeatedly the way you normally do until the faders stop following (you reported it takes a few times).
3. When it stops: note the time, then deliberately trigger 2–3 more updates that should move the faders.
4. The capture records all incoming fader CCs/pitchbend (`MI`) and all outgoing feedback (`MO`) — the point where `MO` lines stop while triggers continue is the evidence.

## B4 — Note-move display lag

1. Record a loop with a moderate number of notes.
2. In NOTE_EDIT, move notes with the faders the way that normally produces the lag.
3. When you notice lag: note the time and keep moving for a few seconds.
4. If you can, also try the same moves on a track with very few notes, to note whether the lag scales with note count.

## Anything else

If any other bug or oddity shows up during the session, keep playing through it (the capture gets it) and note the time + one line of what you saw.

## Afterwards

Drop the capture file path(s) and your per-bug notes into the chat. The capture is parsed offline; B1/B2 evidence is read directly from `RECS`/`BPM` lines, B3 from the `MI`/`MO` stream around the stall.
