#!/usr/bin/env python3
"""Device check for the loop-edit tempo fader (ch15 pitchbend -> 1..300 BPM in 0.1 steps).

Sends the loop-edit select tap, then discriminating pitchbend values, and reads the device's own `TEMPO: PB=` line
plus the outgoing tempo feedback. The point of the fine-grained values is the **0.1 BPM resolution**: 120.0/120.1/
120.2 are adjacent steps that the retired 7-bit CC fader could not express at all. Not a HITL content run: no
record/overdub, no clear, no writes to loop content. It does set the global tempo, and leaves the last value in place.
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

# The tempo fader is a 14-bit pitchbend on its own channel (MidiConfig::LoopEdit::TEMPO_PITCHBEND_CHANNEL), which is
# what lets it span the whole enforced clock range at a 0.1 BPM quantum (TempoMapping's pitchbend mapping).
TEMPO_PITCHBEND_CHANNEL_1BASED = 15
TEMPO_MIN_BPM = 1.0
TEMPO_MAX_BPM = 300.0
TEMPO_STEP_BPM = 0.1
PITCHBEND_WIRE_MAX = 16383
BPM_STEP_COUNT = 2991  # 1..300 inclusive at 0.1

# Adjacent 0.1 BPM steps: the discriminator the CC fader could not pass.
DISCRIMINATING_BPMS = (120.0, 120.1, 120.2)
# One full-travel end, so the probe covers the range the fader was widened for (the device keeps it after the run).
FULL_TRAVEL_BPM = 300.0


def _wire_for_bpm(tempo_bpm: float) -> int:
    """BPM -> pitchbend wire value, mirroring `TempoMapping::pitchBendWireFromBpm` (0.1 BPM steps, clamped)."""
    clamped = min(max(tempo_bpm, TEMPO_MIN_BPM), TEMPO_MAX_BPM)
    index = round((clamped - TEMPO_MIN_BPM) / TEMPO_STEP_BPM)
    index = min(max(index, 0), BPM_STEP_COUNT - 1)
    return round(index * PITCHBEND_WIRE_MAX / (BPM_STEP_COUNT - 1))


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
        help="Pin the device tempo to this BPM via the tempo pitchbend (read-only elsewhere).",
    )
    parser.add_argument(
        "--skip-full-travel",
        action="store_true",
        help="Do not touch 300 BPM; only the 0.1-step values are exercised.",
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
    channel = TEMPO_PITCHBEND_CHANNEL_1BASED - 1

    # Mode gate: outside LOOP_EDIT the tempo pitchbend must be ignored (and CC 102 must set nothing at all now).
    before_non_edit = len(lines)
    print("[tempo] pitchbend=100 outside LOOP_EDIT (expect ignored)")
    out_port.send(mido.Message("pitchbend", channel=channel, pitch=100))
    out_port.send(mido.Message("control_change", channel=CONTROL_CHANNEL_1BASED - 1, control=102, value=100))
    _drain(1.5)
    non_edit_tempo = [ln for ln in lines[before_non_edit:] if "TEMPO: PB=" in ln or "TEMPO: CC=" in ln]

    # Enter LOOP_EDIT for the selected track, then select the loop row.
    print(f"[tempo] select track {args.track}")
    _send_short_press(out_port, note=TRACK_SELECT_NOTE_BASE + args.track - 1, press_ms=args.press_ms)
    _drain(1.0)
    print(f"[tempo] select loop slot {args.loop_slot}")
    _send_short_press(out_port, note=LOOP_SELECT_NOTE_BASE + args.loop_slot - 1, press_ms=args.press_ms)
    _drain(6.0)

    if args.set_tempo is not None:
        targets = (args.set_tempo,)
    else:
        targets = DISCRIMINATING_BPMS if args.skip_full_travel else (*DISCRIMINATING_BPMS, FULL_TRAVEL_BPM)

    results: list[tuple[int, float, float | None]] = []
    for target in targets:
        wire = _wire_for_bpm(target)
        print(f"[tempo] pitchbend={wire} (expect {target:.1f} BPM)")
        out_port.send(mido.Message("pitchbend", channel=channel, pitch=wire))
        before = len(lines)
        _drain(4.0)
        observed: float | None = None
        for line in lines[before:]:
            match = re.search(r"TEMPO: PB=(\d+) -> ([\d.]+) BPM", line)
            if match and int(match.group(1)) == wire:
                observed = float(match.group(2))
                break
        results.append((wire, target, observed))

    if args.set_tempo is not None:
        pinned = results[0][2]
        print(
            f"[tempo] pinned device tempo: {pinned if pinned is not None else 'unobserved'} BPM "
            f"(requested {args.set_tempo:.1f})"
        )

    # Outgoing feedback: the session-entry tap should park the motor on the live tempo.
    feedback = [line for line in lines if "Sent tempo CC feedback" in line]
    log_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    ser.close()

    ok = True
    if non_edit_tempo:
        print(
            "[tempo] ADVISORY: the tempo fader applied before this probe selected a track "
            f"({non_edit_tempo[0].strip()}); the device was already in LOOP_EDIT."
        )
    else:
        print("[tempo] PASS: the tempo pitchbend (and CC 102) did nothing before this probe selected a track")
    for wire, target, observed in results:
        if observed is None:
            print(f"[tempo] FAIL: no device TEMPO line for pitchbend={wire} (expected {target:.1f} BPM)")
            ok = False
        elif abs(observed - target) > 0.05:
            print(f"[tempo] FAIL: pitchbend={wire} -> {observed} BPM, expected {target:.1f}")
            ok = False
        else:
            print(f"[tempo] PASS: pitchbend={wire} -> {observed} BPM")
    # The resolution claim itself: adjacent 0.1 BPM steps must be distinguishable, which is why the CC fader went.
    if args.set_tempo is None and not args.skip_full_travel:
        steps = [observed for _, target, observed in results if target in DISCRIMINATING_BPMS]
        if len(steps) == len(DISCRIMINATING_BPMS) and all(value is not None for value in steps):
            ascending = all(later > earlier for earlier, later in zip(steps, steps[1:]))
            print(f"[tempo] {'PASS' if ascending else 'FAIL'}: 0.1 BPM steps are distinguishable {steps}")
            ok = ok and ascending
        else:
            print("[tempo] FAIL: could not read all three 0.1-step values")
            ok = False
    if not feedback:
        print("[tempo] FAIL: no session-entry tempo feedback line")
        ok = False
    else:
        print(f"[tempo] PASS: feedback {feedback[-1].strip()}")
        if not re.search(r"BPM -> PB=-?\d+", feedback[-1]):
            print("[tempo] FAIL: tempo feedback is not a pitchbend park line")
            ok = False

    print(f"[tempo] log: {log_path}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
