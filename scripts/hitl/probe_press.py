#!/usr/bin/env python3
"""Scheduled button presses from a process of its own.

The successor-reentry probe has to press its two buttons *inside* the predecessor's
deferred-stop window (~1 s on an 8-bar loop). Pressing from the harness process does not
work: the deferred stop floods the same USB serial stream the harness collector parses, and
that parsing starves the harness main thread for more than a second. Measured on device
(``successor-reentry-18``): +400 ms requested, **+1535 ms** measured, and the stop edge was
not even observable inside the anchor timeout. This helper owns its interpreter and its own
MIDI client, so its deadlines are independent of that load.

The harness starts this process, sends the stop press, and then writes one line on stdin.
That line is the schedule's time origin, so the helper's own startup (interpreter, MIDI
client) is not part of the delay. Delays are milliseconds from the origin; ``--denial-delay-ms``
of zero means "no second press".

Usage::

    probe_press.py --midi-out NAME --note 36 --channel 16 --press-ms 120 \
        --successor-delay-ms 500 [--denial-delay-ms 400] [--label1 successor] [--label2 denial]

Every press is a short press (note on, hold, note off).
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import mido  # noqa: E402


def _send_short_press(out_port: mido.ports.BaseOutput, *, note: int, channel_1based: int,
                      press_ms: int) -> None:
    channel = channel_1based - 1
    out_port.send(mido.Message("note_on", channel=channel, note=note, velocity=127))
    time.sleep(max(press_ms, 1) / 1000.0)
    out_port.send(mido.Message("note_off", channel=channel, note=note, velocity=0))


def run(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Scheduled reentry-probe button presses")
    parser.add_argument("--midi-out", required=True, help="MIDI output port name")
    parser.add_argument("--note", type=int, required=True)
    parser.add_argument("--channel", type=int, required=True, help="1-based control channel")
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--successor-delay-ms", type=int, required=True,
                        help="delay of press 1 from the stdin go-ahead")
    parser.add_argument("--denial-delay-ms", type=int, default=0,
                        help="extra delay of press 2 after press 1; 0 disables it")
    parser.add_argument("--label1", default="successor")
    parser.add_argument("--label2", default="denial")
    args = parser.parse_args(argv)

    with mido.open_output(args.midi_out) as out_port:
        print("[probe-press] ready, waiting for the go-ahead", flush=True)
        if not sys.stdin.readline():
            return 1
        origin = time.time()
        schedule = [(args.label1, args.successor_delay_ms)]
        if args.denial_delay_ms > 0:
            schedule.append((args.label2, args.successor_delay_ms + args.denial_delay_ms))
        for label, delay_ms in schedule:
            target = origin + max(delay_ms, 0) / 1000.0
            remaining = target - time.time()
            if remaining > 0:
                time.sleep(remaining)
            _send_short_press(out_port, note=args.note, channel_1based=args.channel,
                              press_ms=args.press_ms)
            print(
                f"[probe-press] {label} press sent ({delay_ms} ms scheduled, "
                f"lateness {(time.time() - target) * 1000:.0f} ms)",
                flush=True,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(run())
