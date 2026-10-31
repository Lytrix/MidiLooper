#!/usr/bin/env python3
"""Dump one loop's committed content and capture a few playback cycles.

Stage 2 of the HITL content-verification recipe (see SKILL.md). Run it while the
device already holds the content under test and transport can be stopped.

By default the target is what the device already has: the **selected** track's
**active** slot. `--select-track` and `--dump-slot` name a different target
without a destructive gesture -- the track button is a `SELECT_TRACK`, and
`--dump-slot` names the slot in the dump request itself, so the loop button
(whose short press is a record toggle) is never pressed and the active slot is
never moved. `--dump-only` writes the dump and skips playback, which is the
read-only residency check.

Writes into --out-dir:
  serial.log         raw USB serial (includes the !CAPTURE_CONTENT dump)
  midi-output.jsonl  every MIDI input message with host_ns
  actions.jsonl      playback start/stop host_ns (cycle window for the verifier)

Requires the .venv interpreters (mido, pyserial):
  .venv/bin/python dump_and_capture.py --out-dir captures/<run>-playback

A named target on a non-active slot (the demoted windowed target):
  .venv/bin/python dump_and_capture.py --out-dir captures/<run>-playback \
      --select-track 8 --dump-slot 4 --capture-seconds 160
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys
import time

import mido
import serial

# The action layer owns the track-select note and the press itself. Importing it from the repo keeps this
# recipe on the canonical gesture instead of re-deriving a note number here.
_ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(_ROOT / "scripts"))

from hitl.actions import slots  # noqa: E402


class _Ctx:
    """Minimal ActionContext stand-in: the slot actions only need `out_port` and `config.press_ms`."""

    class _Cfg:
        press_ms = 120

    def __init__(self, out_port) -> None:
        self.out_port = out_port
        self.config = self._Cfg()


def dump_command(dump_slot: int | None) -> bytes:
    """The dump request. A named slot is 1-based on the CLI and 0-based on the wire, as the firmware reads it."""
    if dump_slot is None:
        return b"!CAPTURE_CONTENT\n"
    return f"!CAPTURE_CONTENT_SLOT {dump_slot - 1}\n".encode()


def _resolve_port(requested: str, names: list[str], kind: str) -> str:
    """Exact match first, then substring: ALSA/CoreMIDI may suffix the port name."""
    if requested in names:
        return requested
    lowered = requested.lower()
    for name in names:
        if lowered in name.lower():
            return name
    raise SystemExit(f"No MIDI {kind} port matching {requested!r}; available: {names}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    parser.add_argument("--port", default="/dev/cu.usbmodem154944801")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--dump-wait-s", type=float, default=8.0)
    parser.add_argument(
        "--boot-settle-s",
        type=float,
        default=10.0,
        help="Seconds to settle after opening serial (opening it resets the board)",
    )
    parser.add_argument("--capture-seconds", type=float, default=52.0)
    parser.add_argument("--send-clocks", action="store_true", default=True)
    parser.add_argument("--no-send-clocks", dest="send_clocks", action="store_false")
    parser.add_argument(
        "--select-track",
        type=int,
        choices=range(1, 9),
        default=None,
        metavar="N",
        help="Short-press track N's button (a SELECT_TRACK) before the dump, so a named slot is read on that "
             "track. Non-destructive: it moves no slot and starts no recording.",
    )
    parser.add_argument(
        "--dump-slot",
        type=int,
        choices=range(1, 9),
        default=None,
        metavar="N",
        help="Dump slot N of the selected track via !CAPTURE_CONTENT_SLOT instead of the active slot. Read-only, "
             "so a populated slot can be inspected without pressing its loop button (a record toggle).",
    )
    parser.add_argument(
        "--solo-track",
        type=int,
        choices=range(1, 9),
        default=None,
        metavar="N",
        help="Long-press track N's button (exclusive SOLO) around the playback capture, so other tracks do "
             "not sound into the served-content capture. Toggled off after the capture. Non-destructive.",
    )
    parser.add_argument(
        "--select-settle-s",
        type=float,
        default=0.6,
        help="Seconds to let the track selection land before the dump request.",
    )
    parser.add_argument("--press-ms", type=int, default=120, help="Track-button hold time.")
    parser.add_argument(
        "--dump-only",
        action="store_true",
        help="Write the dump and stop; skip playback. Read-only residency check.",
    )
    args = parser.parse_args()

    out = args.out_dir
    out.mkdir(parents=True, exist_ok=True)
    actions: list[dict] = []

    with (out / "serial.log").open("wb", buffering=0) as log, \
            (out / "midi-output.jsonl").open("w", buffering=1) as midilog:
        # MIDI ports first, serial last: opening this board's serial port resets it, and the
        # reset briefly removes the USB MIDI ports, so `mido.open_output` after the serial open
        # fails with `OSError: unknown port 'Teensy'`. `boot_settle_s` then lets the device
        # finish booting before the capture starts.
        def recv(message) -> None:
            midilog.write(json.dumps({"host_ns": time.monotonic_ns(), "message": message.dict()}) + "\n")

        with mido.open_output(
                _resolve_port(args.midi_out, mido.get_output_names(), "output")) as port, \
                mido.open_input(
                    _resolve_port(args.midi_in, mido.get_input_names(), "input"),
                    callback=recv):
            with serial.Serial(args.port, args.baud, timeout=0.001) as dev:
                time.sleep(max(args.boot_settle_s, 0.0))

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

                if args.select_track is not None:
                    # Before the stop/drain protocol: a SELECT_TRACK press is not a transport gesture, and the
                    # dump is transport-gated, so this must land while the device is still stopped anyway.
                    slots.press_track(_Ctx(port), args.select_track, press_ms=args.press_ms)
                    observe(max(args.select_settle_s, 0.0))

                port.send(mido.Message("stop")); observe(1.0)
                port.send(mido.Message("stop")); observe(1.0)
                dev.write(dump_command(args.dump_slot))
                observe(args.dump_wait_s)
                if "CAPTURE_CONTENT,end" not in (out / "serial.log").read_text(errors="replace"):
                    raise SystemExit("No complete canonical dump (device stopped? capture build?)")
                if args.dump_only:
                    print(f"Dumped content -> {out}")
                    (out / "actions.jsonl").write_text("")
                    return 0
                soloed = False
                if args.solo_track is not None:
                    # Exclusive solo: only the target track sounds, so other restored tracks cannot appear
                    # in the served-content capture. Cleared after playback below.
                    slots.press_track_solo(_Ctx(port), args.solo_track)
                    observe(0.4)
                    soloed = True
                actions.append({"event": "start", "host_ns": time.monotonic_ns()})
                port.send(mido.Message("start"))
                try:
                    observe(args.capture_seconds, clocks=args.send_clocks)
                finally:
                    port.send(mido.Message("stop"))
                    actions.append({"event": "stop", "host_ns": time.monotonic_ns()})
                    observe(3.0)
                    if soloed:
                        slots.press_track_solo(_Ctx(port), args.solo_track)
                        observe(0.4)

    (out / "actions.jsonl").write_text("".join(json.dumps(a) + "\n" for a in actions))
    print(f"Dumped content and captured playback -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
