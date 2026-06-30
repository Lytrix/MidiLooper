"""Serial verifier for fader_motor_probe scenario."""

from __future__ import annotations

import argparse

from hitl.fader_motor_probe import motor_channel_for_fader, verify_pitchbend_echoes


def _parse_fader(args: object) -> str:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--fader", choices=("fader1", "fader2", "both"), default="fader1")
    legacy = list(getattr(args, "legacy_args", []) or [])
    return parser.parse_args(legacy).fader


def verify_fader_motor_probe(lines: list[str], args: object) -> dict[str, object]:
    fader = _parse_fader(args)
    if fader == "both":
        ch16 = verify_pitchbend_echoes(
            lines,
            channel_1based=motor_channel_for_fader("fader1"),  # type: ignore[arg-type]
            min_echo_count=1,
        )
        ch14 = verify_pitchbend_echoes(
            lines,
            channel_1based=motor_channel_for_fader("fader2"),  # type: ignore[arg-type]
            min_echo_count=1,
        )
        return {
            "fader": "both",
            "ch16": ch16,
            "ch14": ch14,
            "ok": ch16.get("ok", False) and ch14.get("ok", False),
        }
    return verify_pitchbend_echoes(
        lines,
        channel_1based=motor_channel_for_fader(fader),  # type: ignore[arg-type]
        min_echo_count=1,
    )
