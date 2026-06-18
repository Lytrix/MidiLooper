# M8 edit — note-edit HITL automation

Hardware-in-the-loop automation for note-edit overlap scenarios on top of the standard **2-bar record** prelude (no overdub). One combined edit session where possible; exit edit validates persistence / undo.

## Command (target)

Run from project root with serial capture in a second terminal (`scripts/capture_session.py`):

```bash
.venv/bin/python scripts/host_midi_automation_edit_baseline.py \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --midi-channel 5 \
  --record-bars 2 \
  --start-transport \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --verify-serial-log captures/<session>.log
```

Build: **`teensy41-capture-serial`** (needs `#CAP` / `REVT` / `ST` lines).

## Prelude

1. Select track, long-press record → **EMPTY** (same as record/overdub baseline).
2. Short-press record → **RECORDING**; stream **fixture notes** (not dense chromatic grid) for 2 bars on the recordable channel.
3. Stop record → **PLAYING**; wait for `REVT` flush (idle maintenance or `--final-wait-ms`).

### Record fixture (2 bars, 32 × 16th steps)

Deterministic layout for overlap cases (pitch = lane, step = time). Each note = 2 × 16th gate unless noted.

| Step | Tick | Pitch | Role |
|------|------|-------|------|
| 0 | 0 | 60 (C4) | **M0** — primary move subject |
| 4 | 192 | 64 (E4) | **B** — neighbor before |
| 8 | 384 | 67 (G4) | **A** — neighbor after |
| 12 | 576 | 60 (C4) | **P0** — pitch-lane blocker (same pitch as M0) |
| 18 | 864 | 64 (E4) | **D** — delete target |
| 22 | 1056 | 65 (F4) | **L** — sandwich left |
| 26 | 1248 | 69 (A4) | **R** — sandwich right |
| 1 | 48 | — | **Empty** — add-note target (fader 1 + triple 38) |

Steps 16–17 intentionally empty for “move over deleted timeline” after deleting **D**.

## Note selection policy (HITL)

**Do not use bar/step buttons (ch16 notes 0–15) to select notes while transport is playing.** Those buttons move the **current tick / playhead**, not the edit bracket.

| Task | Control |
|------|---------|
| Select note or empty 16th step | **Fader 1 only** — PB ch16 + PC ch16 prog 1 |
| Move selected note (coarse) | **Fader 2 only** — PB ch15 + PC ch15 prog 2 |
| Fine position / pitch | **Fader 3 / 4** — CC ch15 |
| Delete selected | Double **38** ch16 |
| Create at bracket (empty step) | Fader 1 → empty step, triple **38** ch16 |
| Enter / exit edit | Short / long **38** ch16 |

### Fader 1 grid iteration

One fader slot per **16th step** in the loop. Moving fader 1 steps through the grid:

- If a note **starts in that 16th** (`noteStep = startTick / 48`), that note is selected.
- If the step is **empty**, the bracket moves to the empty grid tick (create-note / delete target).
- Fader 1 does **not** move notes — only fader 2 changes note start position after a note is selected.

Re-select with **fader 1** before each **fader 2** move. Allow ~650 ms after fader 1 + ~1200 ms before fader 2 (`NOTE_SELECTION_GRACE` + `COARSE_STABILITY`).

## Edit session flow (single session)

| # | Scenario | MIDI / UI steps | Serial / store checks |
|---|----------|-----------------|------------------------|
| 1 | Enter edit | Short **38** (ch16) | `entered note edit mode` + `NOTE_EDIT` |
| 2 | Add note | Fader 1 → empty step 1, triple **38** | `Created 32nd note` |
| 3 | Remove note | Fader 1 → step 1, double **35** (NOTELEN) | `Deleting note` |
| 4 | Select **M0** | Fader 1 → step 0 | note in step 0 selected |
| 5 | Move over note **before** | Fader 2 → step 4 (over **B**) | `POSITION EDIT` |
| 6 | Move over note **after** | Fader 1 → **M0**, fader 2 → step 8 | same |
| 7–9 | Overlap L/R/sandwich | Coarse moves on **L** / **R** / between | overlap logs |
| 10 | Delete **D** | Fader 1 → step 18, double **35** | `Deleting note` |
| 11–12 | Move over deleted slot | Fader 1 re-select, fader 2 → 17 / 19 | no ghost events |
| 13 | Pitch through **P0** | Fader 1 → **M0**, fader 4 CC3 up | pitch overlap |
| 14 | Exit edit | Long **38** (~700 ms) | `exited edit mode` |
| 15 | Global undo | Double **36** | `Overdub undone` or post-M8 **`NoteEditSessionCommitted`** |

Between move scenarios, re-select the note under edit with **fader 1**, then move with **fader 2**.

## Firmware prerequisites

- [x] Short **38** enters edit overlay + forces **NOTE_EDIT** main mode.
- [x] Double **35** (NOTELEN) → **`DELETE_OR_CREATE_NOTE`** (delete when selected, create at empty bracket).
- [x] Double **38** → delete selected note.
- [ ] Post-M8: exit edit → **`closeNoteEditSpan()`** → single **`NoteEditSessionCommitted`**.

`BarStepButtonHandler::executeNoteEditAction` remains for on-device UI when not playing; **HITL does not drive ch16 step buttons for selection.**

## Verification policy

Same as record/overdub baseline: external serial capture mandatory; fail if transitions missing, heartbeat abort, or fixture `REVT` count/ticks mismatch after record.

Edit-specific: parse serial for delete/create/move log lines; optional `#CAP,REVT` snapshot after exit (may require flush wait). After M8, assert **`NoteEditSessionCommitted`** once on exit.

## Native tests (m8-edit §4)

Host matrix covers the same scenarios without hardware; HITL is the integration gate for fader timing + DROID routing.
