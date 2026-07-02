# NOTE_EDIT select-dependent faders HITL — enhancement

Host scenario and verifier for **F2/F3/F4 motor feedback** on live **F1 note select** during NOTE_EDIT. Gates §7.18 in `note-edit-fader-feedback-regression` and acceptance in `note-edit-stable-note-id` §7.3.

## Artifacts

| Artifact | Path |
|----------|------|
| Scenario | `scripts/hitl/scenarios/note_edit_select_dependent_faders.py` |
| Verifier | `scripts/hitl/verify/note_edit_select_dependent_faders.py` |
| Host unit test | `scripts/test_note_edit_select_dependent_faders_serial_verify.py` |
| Preset | `note_edit_select_dependent_faders` in `scripts/hitl/registry.py` |

**Out of scope:** `scripts/hitl/fader_motor_probe.py` and `fader_motor_probe` / `fader_motor_sweep` presets (unchanged).

## Run

Firmware: `teensy41-capture-serial` (`#CAP`, `#DBG select_apply`, `outbound_ctx`).

The preset runs **`base`** then **`note_edit_select_dependent_faders`** in one `host_midi_hitl` invocation (runner aborts if base fails). Same as `revision_load_record`.

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset note_edit_select_dependent_faders \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --dwell-ms 800 --toggle-dwell-ms 800 --toggle-cycles 6
```

**Verify-only** on a captured log:

```bash
.venv/bin/python scripts/host_midi_hitl.py run \
  --scenarios note_edit_select_dependent_faders \
  --verify-serial-log captures/note_edit_select_dependent_faders_<stamp>_serial.log \
  --verify-only
```

**Reuse seed:** `--scenarios note_edit_select_dependent_faders --seed-serial-log captures/<base>.log` after a passing base run.

## Verifier gates (per dwell cluster — last `apply=1 reason=note_changed` in cluster)

Composes (no edits to) `note_edit_select_triple_motor_ack`, `fader_motor_echo_correlation`.

During an F1 sweep many `note_changed` applies may occur; motors flush once after **600 ms** F1 idle. Scenario `dwell-ms=800` allows idle + burst.

| Gate | Threshold |
|------|-----------|
| Timely | `select_motor_sync sent=1` **600–950 ms** after cluster end; F2/F3/F4 MO ≤ **400 ms** after sync |
| Consistent | One full F2+F3+F4 MO + ch13 acks 83/85/87 per dwell cluster; no motor on `unchanged_note` |
| Values | MO F2 pb / F4 cc match `#DBG outbound_ctx` or `select_motor_sync` plan |
| RC11 signal | `pb != expected_pb_rel` on `outbound_ctx f2` reported (non-fatal count) |
| Perceptual | Optional `--min-perceptual-f2/f4` (default 0 for baseline capture) |

## Phase 2 (firmware)

After baseline capture on current firmware: fix `send*FromSelectTarget` to resolve geometry from `EditorSelection.primaryNote` (not bracket-only F2 tick). See `note-edit-fader-feedback-regression` tasks §7.22.
