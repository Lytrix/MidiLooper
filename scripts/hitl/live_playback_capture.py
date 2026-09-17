#!/usr/bin/env python3
"""Capture the device's LIVE playback without opening serial (which would reset it).

Isolates the live publisher from the save/restore path: open MIDI only, stop the
transport, start it, and record the outgoing messages. Compare the row multiset
against the content dump's `effective` section.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import time

import mido


def _resolve(requested: str, names: list[str], kind: str) -> str:
    if requested in names:
        return requested
    for name in names:
        if requested.lower() in name.lower():
            return name
    raise SystemExit(f"No MIDI {kind} port matching {requested!r}; available: {names}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--settle-s", type=float, default=3.0)
    parser.add_argument("--capture-seconds", type=float, default=44.0)
    args = parser.parse_args()

    out = args.out_dir
    out.mkdir(parents=True, exist_ok=True)
    log = (out / "midi-output.jsonl").open("w", buffering=1)

    def recv(message) -> None:
        log.write(json.dumps({"host_ns": time.monotonic_ns(), "message": message.dict()}) + "\n")

    with mido.open_output(_resolve(args.midi_out, mido.get_output_names(), "output")) as port, \
            mido.open_input(_resolve(args.midi_in, mido.get_input_names(), "input"), callback=recv):
        time.sleep(max(args.settle_s, 0.0))
        port.send(mido.Message("stop"))
        time.sleep(1.0)
        port.send(mido.Message("stop"))
        time.sleep(1.0)
        port.send(mido.Message("start"))
        end = time.monotonic() + args.capture_seconds
        next_clock = time.monotonic()
        while time.monotonic() < end:
            if time.monotonic() >= next_clock:
                port.send(mido.Message("clock"))
                next_clock += 1 / 48
            time.sleep(0.0005)
        port.send(mido.Message("stop"))
        time.sleep(1.0)
    log.close()
    print(f"live playback captured (no serial open, no reset) -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
