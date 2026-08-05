# HITL test scenarios — overview

Hardware-in-the-loop (HITL) tests drive a connected Teensy over USB MIDI and (when required) USB serial
from the host Mac. Entry point: [`scripts/host_midi_hitl.py`](../../scripts/host_midi_hitl.py).

**Firmware:** use `teensy41-capture-serial` so `#CAP`, `ST`, and track undo/redo lines are available. Default HITL run assumes firmware is already on the device; pass `--build-upload` only when you changed firmware.

**Serial capture modes:**

| Mode | Serial | HITL flags |
|------|--------|------------|
| **B (preferred)** | `capture_session.py` in terminal 1 | `--follow-current-session` — no `--serial-port` |
| **A** | Built into HITL | `--serial-port` + `--boot-settle-ms 10000` — no `capture_session.py` |

Never use `--serial-port` while `capture_session.py` holds the USB serial port.

**List scenario IDs:**

```bash
.venv/bin/python scripts/host_midi_hitl.py run --list-scenarios
```

**Verify-only** (replay a captured serial log without MIDI):

```bash
.venv/bin/python scripts/host_midi_hitl.py run \
  --scenarios <scenario_id> \
  --verify-serial-log captures/<log>.log --verify-only
```

Replace `/dev/cu.usbmodem154944801` with your port (`pio device list`).

---

## Presets vs scenarios

| Kind | Meaning |
|------|---------|
| **Preset** | Named bundle of one or more scenarios (`--preset base`) |
| **Scenario** | Single composable phase script (`--scenarios edit_overdub_during_note_edit`) |

Presets are defined in [`scripts/hitl/registry.py`](../../scripts/hitl/registry.py) (`PRESET_SCENARIOS`).

---

## Scenario catalog

| ID | Preset | What it exercises | Serial required | Host verifier |
|----|--------|-------------------|-----------------|---------------|
| `base` | `base` | Clear → record → overdub(s) → optional global undo/redo after last overdub | Recommended | Legacy baseline JSON report |
| `edit_record_prelude` | `edit_full`, `edit_minimal` | 2-bar fixture record for edit suites | Recommended | Record transitions |
| `edit_minimal` | `edit_minimal` | Prelude → enter edit → add/delete/move/length → exit | Recommended | Session enter subset |
| `edit_full` | `edit_full` | Prelude → full note-edit overlap suite | Recommended | Legacy edit baseline gates |
| `edit_overdub_during_note_edit` | `edit_overdub_during_note_edit` | **record_seed** (EDIT_RECORD_FIXTURE) → overdub → edit → in-edit overdub → **E:** session undo/redo → exit → global undo | **Yes** | `verify_edit_overdub_during_note_edit` |
| `long_loop_display_window` | `long_loop_display_window` | 24+ bar record → NOTE_EDIT window freeze → play/stop long-press snap → hold-to-track | **Yes** | `verify_long_loop_display_window` |
| `revision_commit_save` | `revision_commit_save` | Transport stop (current epoch) → `!REV_COMMIT` → `!REV_CLEANUP` (no catalog pollution) | **Yes** | `verify_revision_commit_save` |
| `revision_load` | `revision_load` | Transport stop → `!REV_COMMIT` → `!REV_LOAD` → `!REV_CLEANUP` | **Yes** | `verify_revision_load` |
| `revision_load_record` | `base`, `revision_load_post_record` | Base record/overdub → commit → load (loop data) | **Yes** | `verify_revision_load` |
| `fader_motor_probe` | `fader_motor_probe` | NOTE_EDIT arm + fader motor pitchbend/note-0 steps (host → Teensy → DROID) | Optional | `verify_fader_motor_probe` |
| `fader_motor_sweep` | `fader_motor_sweep` | Quarter sweep 0 % → 25 % → 50 % → 75 % → 100 % on fader1 (default) | Optional | `verify_fader_motor_probe` |
| `note_edit_select_dependent_faders` | `base`, `note_edit_select_dependent_faders` | **base** (2+2 + 2 overdub) then NOTE_EDIT F1 sweep + toggle | **Yes** | `verify_note_edit_select_dependent_faders` |

### `revision_commit_save` (packed revision write + cleanup)

Requires `teensy41-capture-serial` (SESSION_CAPTURE). Host sends serial `!REV_COMMIT` / `!REV_CLEANUP`; firmware backs up `index.bin` / `set.bin`, commits `v####.bin`, then restores catalog state and deletes the test revision.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset revision_commit_save \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 \
  --phase-wait-ms 500 --press-ms 120 \
  --prior-save-drain-wait-ms 120000 \
  --deferred-save-wait-ms 120000 \
  --revision-commit-wait-ms 180000
```

Run after `base` preset: the scenario waits for the prior deferred save to finish before `!REV_COMMIT`. Serial may emit `#CAP,...,PERS,rev_blocked,...,deferred_save_active` while blocked.

Use `--skip-hitl-cleanup` only when debugging a failed commit (leaves revision on SD).

### `revision_load` (packed revision commit + deferred load + cleanup)

Requires `teensy41-capture-serial` (SESSION_CAPTURE). Commits a revision from Current, loads it back with `!REV_LOAD <setId> <revisionId>`, then restores catalog state with `!REV_CLEANUP`.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset revision_load \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 \
  --phase-wait-ms 500 --press-ms 120 \
  --prior-save-drain-wait-ms 120000 \
  --deferred-save-wait-ms 120000 \
  --revision-commit-wait-ms 180000 \
  --revision-load-wait-ms 180000
```

Use `--skip-hitl-cleanup` when debugging a failed load. Optional dev wipe of all sets: `--nuke-sets-before-run` (synchronous SD — can stall USB; not part of default pass criteria).

Host unit test: `scripts/test_revision_load_serial_verify.py`.

### `revision_load_record` (base record → commit → load)

Runs canonical **`base`** record/overdub on the selected track (preset injects bar-synced 2+2 baseline flags from HITL-Test-Flow), then **`revision_load_post_record`** (skips transport-stop prelude and the pre-commit deferred-save drain — base already flushed loops to SD). Use this when verifying loop data round-trips through a revision.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset revision_load_record \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5
```

### `base` (record/overdub baseline)

Default second overdub pass is on (2 bars, C0–B1). Undo/redo after last overdub stop is on by default.

**Mode B (preferred)** — start `capture_session.py` first, then:

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset base \
  --midi-out "Teensy" --midi-in "Teensy" \
  --follow-current-session \
  --track-number 5 --midi-channel 5 \
  --record-bars 2 --overdub-bars 2 \
  --no-fixed-grid-notes --start-transport \
  --overdub-start-delay-bars 0 --overdub-start-delay-beats 1 \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 3000
```

**Mode A** — single terminal, built-in serial (no external capture):

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset base \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --boot-settle-ms 10000 \
  --track-number 5 --midi-channel 5 \
  --record-bars 2 --overdub-bars 2 \
  --no-fixed-grid-notes --start-transport \
  --overdub-start-delay-bars 0 --overdub-start-delay-beats 1 \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 3000
```

Opt-in firmware flash before run: add `--build-upload` (restarts Teensy — restart capture after).

Legacy entry: `scripts/host_midi_automation_baseline.py` (delegates to preset `base`).

### `long_loop_display_window`

Loop must be **> 16 bars** for bounded detailed window.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset long_loop_display_window \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --record-bars 24 --start-transport \
  --freeze-wait-ms 8000 --hold-track-ms 4000 --long-press-ms 700 \
  --phase-wait-ms 500 --press-ms 120
```

Host unit test: `scripts/test_long_loop_display_serial_verify.py`.

### `edit_full` / `edit_minimal`

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset edit_full \
  --midi-out "Teensy" --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --record-bars 2 --start-transport
```

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset edit_minimal \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track 5 --midi-channel 5 \
  --loop-slot 2 \
  --record-bars 2 --start-transport
```

`--loop-slot` drives **record arm** on the Loops row (notes 50–57) during the base seed; omit it to use Record button 36 on the device-selected slot. The base phase logs `[info] --loop-slot N on track …`.

Legacy entry: `scripts/host_midi_automation_edit_baseline.py`.

### `edit_overdub_during_note_edit`

Layered preset runs **`record_seed`** (2-bar `EDIT_RECORD_FIXTURE` via the proven clear/arm path) then the edit+overdub body. Legacy `--preset edit_overdub_during_note_edit` runs the same two phases (`base` seed + body).

```bash
.venv/bin/python scripts/host_midi_hitl.py run --layered --preset edit_overdub_during_note_edit \
  --midi-out "Teensy" --midi-in "Teensy" \
  --track-number 5 --loop-slot 2 --midi-channel 5
```

**E:** session undo after in-edit overdub targets the in-edit overdub layer only. Pre-edit overdub stays.

### `fader_motor_probe` / `fader_motor_sweep`

Isolated DROID motorfader check: host USB MIDI → Teensy (`teensy41-capture-serial-fader-probe`) → DROID
USB host. See **[`DROID_MOTORFADER_PITCHBEND.md`](DROID_MOTORFADER_PITCHBEND.md)** for pitchbend scale,
NOTE_EDIT arm (~50 % snap), and **`pitch_note_off`** trigger ordering.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset fader_motor_sweep \
  --midi-out "Teensy" --midi-in "Teensy" --settle-ms 2500
```

Optional: `--fader fader2`, `--fader both` (ch16 + ch14 same pitchbend/trigger per step),
`--timing note_pitch_off` (legacy wrong order), `--no-edit-state-pc`.

### NOTE_EDIT fader select refresh (Phase 3 verify)

Manual repro on `teensy41-capture-serial`: fast fader1 sweep then slow final creep; fader1 to physical min.
Wait ≥400 ms quiet; confirm F2/F3/F4 motors match final note. Serial verifier (capture log):

```bash
.venv/bin/python -c "
from hitl.verify.note_edit_fader_select_refresh import verify_note_edit_fader_select_refresh
import sys
lines = open(sys.argv[1]).read().splitlines()
print(verify_note_edit_fader_select_refresh(lines))
" captures/session_YYYYMMDD_HHMMSS.log
```

Pass: each fader1 inbound cluster has `MO,224,14` or `#DBG outbound_step=SEND_F2` within 3 s;
max gap between F2 bursts ≤30 s; pipeline shows `BEGIN` → `ARM` → `SEND_F2` → `TRIGGER_F2` → … → `DONE`
and/or `QUIET_REFRESH` after user-classified quiet.

### `note_edit_select_dependent_faders` (F2/F3/F4 motor sync on F1 select)

Runs **`base`** then **`note_edit_select_dependent_faders`** (same pattern as `revision_load_record`). Runner aborts if base fails. Plan: [`note_edit_select_dependent_faders_hitl_enhancement.md`](../plans/note_edit_select_dependent_faders_hitl_enhancement.md).

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset note_edit_select_dependent_faders \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --dwell-ms 800 --toggle-dwell-ms 800 --toggle-cycles 6
```

Or run phases in one shell session: `--preset note_edit_select_dependent_faders` (recommended). Split runs: `--preset base` then `--scenarios note_edit_select_dependent_faders` only when the device loop still matches the latest passing base report.

Verify-only (sweep log): `--scenarios note_edit_select_dependent_faders --verify-serial-log captures/note_edit_select_dependent_faders_*_serial.log --verify-only`

### Geometry F1 motor sync guard (host verifier)

After geometry-driven F1 motor flush, inbound F1 must not trigger `select_apply` or exit Move state within **1500 ms**. Host unit tests (no Teensy):

```bash
.venv/bin/python scripts/test_note_edit_geometry_fader1_serial_verify.py
```

Manual: move note with F2/F3 in Move kind; confirm F1 follows and selection/kind stay on moving note. Plan: [`note_edit_geometry_f1_selection_guard_bugfix.md`](../plans/note_edit_geometry_f1_selection_guard_bugfix.md).

---

## Native (host) tests vs HITL

| Suite | Command | Teensy |
|-------|---------|--------|
| Firmware logic | `pio test -e native` | Not required |
| HITL verifier scripts | `.venv/bin/python scripts/test_*_serial_verify.py` | Not required |
| Full HITL run | `host_midi_hitl.py run …` | **Required** |

Run `pio test -e native` before push/merge. Run relevant HITL preset when changing capture, undo, display, or edit flows covered above.

---

## Manual regression (firmware invariant review)

Hardware steps from [`firmware_ownership_lifetime_review.md`](../plans/firmware_ownership_lifetime_review.md). Archive capture as `captures/MT-<id>_<date>.log`.

| ID | Covers | Status |
|----|--------|--------|
| **MT-P0** | Materialize stale after loop-length change with committed editPass | Conditional PASS — `session_20260805_222144.log` |
| **MT-P1-undo** | **E:** vs **U:** routing during NOTE_EDIT (session-gated; no global fallthrough) | **PASS** — `session_20260806_003023.log` |
| **MT-P1-fold** | Unified wrap finalize (seal + in-edit fold) | Pending |
| **MT-P1-display-audition** | NOTE_EDIT geometry while PLAYING (no hang) | In progress |
| **MT-P5-recovery** | Phase 5 longest valid prefix load | Blocked — Phase 5 code |
| **MT-hygiene** | Smoke after doc-only / low-risk | Optional |

**MT-P1-undo quick check:** enter NOTE_EDIT → two geometry actions → drain **E:** → press global undo while still in edit → serial must show `No session undo available` and **not** `Scoped edit pass undone` → exit edit → global undo must run.

---

## Related docs

- Implementation notes: [`docs/plans/hitl_modular_scenarios_enhancement.md`](../plans/hitl_modular_scenarios_enhancement.md)
- DROID motorfader pitchbend / probe timing: [`DROID_MOTORFADER_PITCHBEND.md`](DROID_MOTORFADER_PITCHBEND.md)
- Cursor rule (canonical `base` command): [`.cursor/rules/HITL-Test-Flow.mdc`](../../.cursor/rules/HITL-Test-Flow.mdc)
- Undo/redo storage rules: [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](LOOP_MIDI_STORAGE_AND_VALIDATION.md)
