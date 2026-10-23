#!/usr/bin/env python3
"""Minimal NOTE_EDIT device oracle for the qualification route.

Proves, on the device, that the NOTE_EDIT route opens an edit session on a selected note and that a coarse fader move
changes that note's stored position. Independent of the legacy `edit_full` fixture (which has been chronically failing
on this bench; see the 2026-10-22 3.15f evidence).

Expected seed: the 2-bar prelude layout (one note per step; the oracle reads the source note's exact tick from the
read-only dump, so a one-tick record offset is tolerated).

Sequence (DROID control surface):
  1. short track-select press for the target track;
  2. short EDIT press to enter NOTE_EDIT from LOOP_EDIT;
  3. program change 1 + select-fader pitchbend for the source step (opens the session on that note);
  4. program change 2 + coarse-fader pitchbend for the destination step.

Verification: a `!CAPTURE_CONTENT` read-only dump after the move must show the source pitch's tick near the
destination step and no longer near the source step. Exits 0 only then. The note is left moved; re-record the seed
before a repeat.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import mido
import serial

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from hitl.control_constants import TICKS_PER_16TH_STEP  # noqa: E402
from hitl.select_mapping import pitchbend_for_step_and_stack  # noqa: E402

CONTROL_CHANNEL_1BASED = 16
SELECT_CHANNEL_1BASED = 16
COARSE_CHANNEL_1BASED = 14
EDIT_BUTTON_NOTE = 38
TRACK_SELECT_NOTE_BASE = 60


def _read_edit_mode(ser: serial.Serial, timeout_s: float) -> str | None:
    """Read the firmware's own session line; INFO is on the console. Returns NOTE_EDIT/LOOP_EDIT or None."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = ser.readline().decode("utf-8", errors="replace")
        if "Edit session: NOTE_EDIT" in line:
            return "NOTE_EDIT"
        if "Edit session: LOOP_EDIT" in line:
            return "LOOP_EDIT"
    return None


def _enter_note_edit(ser: serial.Serial, out: mido.ports.BaseOutput) -> bool:
    """Toggle the EDIT button until the firmware reports NOTE_EDIT (it may already be there or be LOOP_EDIT)."""
    for _ in range(2):
        _press(out, EDIT_BUTTON_NOTE, channel_1based=CONTROL_CHANNEL_1BASED)
        mode = _read_edit_mode(ser, 2.5)
        if mode == "NOTE_EDIT":
            return True
    return False


def _press(out: mido.ports.BaseOutput, note: int, *, channel_1based: int, down_ms: int = 80) -> None:
    out.send(mido.Message("note_on", channel=channel_1based - 1, note=note, velocity=127))
    time.sleep(down_ms / 1000.0)
    out.send(mido.Message("note_off", channel=channel_1based - 1, note=note, velocity=0))


def _dump_effective_notes(out_dir: Path, *, track: int, slot: int) -> list[tuple[int, int]]:
    cmd = [
        str(REPO / ".venv/bin/python"),
        str(REPO / ".agents/skills/hitl-content-verification/scripts/dump_and_capture.py"),
        "--out-dir", str(out_dir),
        "--select-track", str(track),
        "--dump-slot", str(slot),
        "--boot-settle-s", "6",
        "--dump-only",
    ]
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    rows: list[tuple[int, int]] = []
    for line in (out_dir / "serial.log").read_text(errors="replace").splitlines():
        if "CAPTURE_CONTENT,event,effective," not in line:
            continue
        fields = line.strip().split(",")
        try:
            status = int(fields[5])
            note = int(fields[7])
            tick = int(fields[4])
        except (IndexError, ValueError):
            continue
        if status & 0xF0 == 0x90:
            rows.append((tick, note))
    return rows


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--midi-out", default="Teensy MIDI")
    parser.add_argument("--track", type=int, default=5)
    parser.add_argument("--slot", type=int, default=1)
    parser.add_argument("--port", default=None)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--from-step", type=int, default=5)
    parser.add_argument("--to-step", type=int, default=7)
    parser.add_argument("--pitch", type=int, default=64)
    parser.add_argument("--out-dir", type=Path, default=None, help="dump directory (default: temp)")
    parser.add_argument("--loop-length", type=int, default=1536)
    args = parser.parse_args(argv)

    port = args.port
    if port is None:
        ports = sorted(Path("/dev").glob("cu.usbmodem*"))
        if not ports:
            print("[note-edit-probe] FAILED: no /dev/cu.usbmodem*")
            return 1
        port = str(ports[0])

    select_pb = pitchbend_for_step_and_stack(args.from_step, args.loop_length, 0, 1)
    coarse_pb = pitchbend_for_step_and_stack(args.to_step, args.loop_length, 0, 1)
    print(
        f"[note-edit-probe] track {args.track} steps {args.from_step}->{args.to_step} pitch {args.pitch} "
        f"pb select={select_pb} coarse={coarse_pb} port={port}"
    )

    ser = serial.Serial(port, args.baud, timeout=0.2)
    out = mido.open_output(args.midi_out)
    try:
        # Stops are idempotent and make the transport state explicit before any edit gesture.
        out.send(mido.Message("stop"))
        time.sleep(0.4)
        out.send(mido.Message("stop"))
        time.sleep(0.4)
        _press(out, TRACK_SELECT_NOTE_BASE + (args.track - 1), channel_1based=CONTROL_CHANNEL_1BASED)
        time.sleep(0.6)
        if not _enter_note_edit(ser, out):
            print("[note-edit-probe] FAILED: could not establish NOTE_EDIT mode")
            return 1
        print("[note-edit-probe] NOTE_EDIT mode confirmed")
        time.sleep(0.4)
        # Program change selects the fader bank; the pitchbend carries the value. Do NOT send note 0:
        # the MOTOR_TRIGGER_NOTE is firmware->DROID motor feedback, and feeding it back suppresses selection.
        out.send(mido.Message("program_change", channel=SELECT_CHANNEL_1BASED - 1, program=1))
        time.sleep(0.2)
        out.send(mido.Message("pitchwheel", channel=SELECT_CHANNEL_1BASED - 1, pitch=select_pb))
        time.sleep(1.2)
        out.send(mido.Message("program_change", channel=COARSE_CHANNEL_1BASED - 1, program=2))
        time.sleep(0.3)
        out.send(mido.Message("pitchwheel", channel=COARSE_CHANNEL_1BASED - 1, pitch=coarse_pb))
        time.sleep(1.5)
    finally:
        ser.close()
        out.close()

    # The content dump is the oracle. Do not require the DEBUG serial markers (the capture session raises the level).
    out_dir = args.out_dir or Path(tempfile.mkdtemp(prefix="note-edit-probe-"))
    out_dir.mkdir(parents=True, exist_ok=True)
    rows = _dump_effective_notes(out_dir, track=args.track, slot=args.slot)
    src_tick = args.from_step * TICKS_PER_16TH_STEP
    dst_tick = args.to_step * TICKS_PER_16TH_STEP
    tol = TICKS_PER_16TH_STEP
    at_src = [t for t, n in rows if n == args.pitch and abs(t - src_tick) <= tol]
    at_dst = [t for t, n in rows if n == args.pitch and abs(t - dst_tick) <= tol]
    print(f"[note-edit-probe] dump {out_dir} note-ons={rows}")
    if at_dst and not at_src:
        print(f"[note-edit-probe] PASS: pitch {args.pitch} moved {src_tick}->{dst_tick} (rows at dst={at_dst})")
        return 0
    print(
        f"[note-edit-probe] FAILED: pitch {args.pitch} at src={at_src} dst={at_dst} "
        f"(expected dst only)"
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
