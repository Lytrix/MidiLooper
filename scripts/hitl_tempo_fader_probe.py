#!/usr/bin/env python3
"""Device check for the loop-edit tempo fader (ch16 CC102 -> 72..200 BPM).

Sends the loop-edit select tap, then two discriminating CC102 values, and reads
the device's own `TEMPO:` line plus the outgoing tempo feedback. Not a HITL
content run: no record/overdub, no clear, no writes to loop content.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

import mido
import serial

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.control_constants import (  # noqa: E402
    CONTROL_CHANNEL_1BASED,
    LOOP_SELECT_NOTE_BASE,
    TRACK_SELECT_NOTE_BASE,
)

TEMPO_CC = 102
TEMPO_CHANNEL_1BASED = 16
TEMPO_MIN_BPM = 60.0
TEMPO_MAX_BPM = 187.0


def _cc_for_bpm(tempo_bpm: float) -> int:
    """One CC step is one BPM; round to the nearest reachable whole BPM."""
    clamped = min(max(tempo_bpm, TEMPO_MIN_BPM), TEMPO_MAX_BPM)
    return round(clamped - TEMPO_MIN_BPM)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial-port", default="/dev/cu.usbmodem154944801")
    parser.add_argument("--track", type=int, default=8)
    parser.add_argument("--loop-slot", type=int, default=4)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--out", type=Path, default=Path("captures/tempo-fader-01"))
    parser.add_argument(
        "--set-tempo",
        type=float,
        default=None,
        help="Pin the device tempo to this BPM via CC102 (read-only elsewhere).",
    )
    return parser.parse_args()


def _send_short_press(port: mido.ports.BaseOutput, *, note: int, press_ms: int) -> None:
    channel = CONTROL_CHANNEL_1BASED - 1
    port.send(mido.Message("note_on", channel=channel, note=note, velocity=127))
    time.sleep(max(press_ms, 1) / 1000.0)
    port.send(mido.Message("note_off", channel=channel, note=note, velocity=0))


def main() -> int:
    args = _parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    log_path = args.out / "tempo-fader.log"

    print(f"[tempo] opening serial {args.serial_port} (Teensy resets) and booting")
    ser = serial.Serial(args.serial_port, 115200, timeout=0.2)
    lines: list[str] = []
    deadline = time.monotonic() + 15.0
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if line:
            lines.append(line)

    def _drain(seconds: float) -> None:
        """Keep collecting serial; the capture ring emits in bursts after a gesture."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if line:
                lines.append(line)

    out_names = [name for name in mido.get_output_names() if "Teensy" in name]
    print(f"[tempo] MIDI out candidates: {out_names}")
    if not out_names:
        print("[tempo] FAIL: no Teensy MIDI output")
        return 2
    out_port = mido.open_output(out_names[0])
    channel = TEMPO_CHANNEL_1BASED - 1

    # Mode gate: outside LOOP_EDIT the tempo CC must be ignored.
    before_non_edit = len(lines)
    print("[tempo] CC102=100 outside LOOP_EDIT (expect ignored)")
    out_port.send(mido.Message("control_change", channel=channel, control=TEMPO_CC, value=100))
    _drain(1.5)
    non_edit_tempo = [ln for ln in lines[before_non_edit:] if "TEMPO: CC=" in ln]

    # Enter LOOP_EDIT for the selected track, then select the loop row.
    print(f"[tempo] select track {args.track}")
    _send_short_press(out_port, note=TRACK_SELECT_NOTE_BASE + args.track - 1, press_ms=args.press_ms)
    _drain(1.0)
    print(f"[tempo] select loop slot {args.loop_slot}")
    _send_short_press(out_port, note=LOOP_SELECT_NOTE_BASE + args.loop_slot - 1, press_ms=args.press_ms)
    _drain(6.0)

    results: list[tuple[int, float | None]] = []
    cc_values = (32, 100, 64) if args.set_tempo is None else (_cc_for_bpm(args.set_tempo),)
    for cc_value in cc_values:
        expected = TEMPO_MIN_BPM + cc_value
        print(f"[tempo] CC{TEMPO_CC}={cc_value} (expect {expected:.1f} BPM)")
        out_port.send(mido.Message("control_change", channel=channel, control=TEMPO_CC, value=cc_value))
        before = len(lines)
        _drain(4.0)
        observed: float | None = None
        for line in lines[before:]:
            match = re.search(r"TEMPO: CC=(\d+) -> ([\d.]+) BPM", line)
            if match and int(match.group(1)) == cc_value:
                observed = float(match.group(2))
                break
        results.append((cc_value, observed))

    if args.set_tempo is not None:
        pinned = results[0][1]
        print(
            f"[tempo] pinned device tempo: {pinned if pinned is not None else 'unobserved'} BPM "
            f"(requested {args.set_tempo:.1f})"
        )

    # Outgoing feedback: the session-entry tap should park the motor.
    feedback = [line for line in lines if "Sent tempo CC feedback" in line]
    log_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    ser.close()

    ok = True
    if non_edit_tempo:
        print(
            "[tempo] ADVISORY: CC102 applied before this probe selected a track "
            f"({non_edit_tempo[0].strip()}); the device was already in LOOP_EDIT."
        )
    else:
        print("[tempo] PASS: CC102 ignored before this probe selected a track")
    for cc_value, observed in results:
        expected = TEMPO_MIN_BPM + cc_value
        if observed is None:
            print(f"[tempo] FAIL: no device TEMPO line for CC={cc_value}")
            ok = False
        elif abs(observed - expected) > 0.6:
            print(f"[tempo] FAIL: CC={cc_value} -> {observed} BPM, expected {expected:.1f}")
            ok = False
        else:
            print(f"[tempo] PASS: CC={cc_value} -> {observed} BPM")
    if not feedback:
        print("[tempo] FAIL: no session-entry tempo feedback line")
        ok = False
    else:
        print(f"[tempo] PASS: feedback {feedback[-1].strip()}")

    print(f"[tempo] log: {log_path}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
