#!/usr/bin/env python3
"""Dump one loop's committed content and capture a few playback cycles.

Stage 2 of the HITL content-verification recipe (see SKILL.md). Run it while the
device already holds the content under test and transport can be stopped.

Writes into --out-dir:
  serial.log         raw USB serial (includes the !CAPTURE_CONTENT dump)
  midi-output.jsonl  every MIDI input message with host_ns
  actions.jsonl      playback start/stop host_ns (cycle window for the verifier)

Requires the .venv interpreters (mido, pyserial):
  .venv/bin/python dump_and_capture.py --out-dir captures/<run>-playback
"""
from __future__ import annotations

import argparse
import json
import pathlib
import time

import mido
import serial


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    parser.add_argument("--port", default="/dev/cu.usbmodem154944801")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--dump-wait-s", type=float, default=8.0)
    parser.add_argument("--capture-seconds", type=float, default=52.0)
    parser.add_argument("--send-clocks", action="store_true", default=True)
    parser.add_argument("--no-send-clocks", dest="send_clocks", action="store_false")
    args = parser.parse_args()

    out = args.out_dir
    out.mkdir(parents=True, exist_ok=True)
    actions: list[dict] = []

    with (out / "serial.log").open("wb", buffering=0) as log, \
            (out / "midi-output.jsonl").open("w", buffering=1) as midilog, \
            serial.Serial(args.port, args.baud, timeout=0.001) as dev, \
            mido.open_output(args.midi_out) as port:

        def recv(message) -> None:
            midilog.write(json.dumps({"host_ns": time.monotonic_ns(), "message": message.dict()}) + "\n")

        def observe(seconds: float, clocks: bool = False) -> None:
            end = time.monotonic() + seconds
            next_clock = time.monotonic()
            while time.monotonic() < end:
                if clocks and time.monotonic() >= next_clock:
                    port.send(mido.Message("clock"))
                    next_clock += 1 / 48
                chunk = dev.read(dev.in_waiting or 1)
                if chunk:
                    log.write(chunk)

        with mido.open_input(args.midi_in, callback=recv):
            port.send(mido.Message("stop")); observe(1.0)
            port.send(mido.Message("stop")); observe(1.0)
            dev.write(b"!CAPTURE_CONTENT\n")
            observe(args.dump_wait_s)
            if "CAPTURE_CONTENT,end" not in (out / "serial.log").read_text(errors="replace"):
                raise SystemExit("No complete canonical dump (device stopped? capture build?)")
            actions.append({"event": "start", "host_ns": time.monotonic_ns()})
            port.send(mido.Message("start"))
            try:
                observe(args.capture_seconds, clocks=args.send_clocks)
            finally:
                port.send(mido.Message("stop"))
                actions.append({"event": "stop", "host_ns": time.monotonic_ns()})
                observe(3.0)

    (out / "actions.jsonl").write_text("".join(json.dumps(a) + "\n" for a in actions))
    print(f"Dumped content and captured playback -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
