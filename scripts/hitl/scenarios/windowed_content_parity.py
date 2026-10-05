"""Task 6.4's windowed leg: the existing long-loop content-verification leg, read for the windowed lane.

The design's own rule is that F-7/F-8 *feed* the content-verification pipeline rather than building a second one
(`design.md`: "fed the windowed/demoted cases (new stimulus profile), not replaced"). So this module adds no driver of
its own: it reuses `run_long_loop_display_window` -- a leg that records a loop far longer than the served window
(`--record-bars`, default 24, and its own run refuses <= 16), keeps the transport running, and already has a host-tested
verifier -- and pairs it with the windowed lane's own checks (`hitl.verify.windowed_lane`).

**The one thing this wrapper owns is the target.** The reused leg takes no slot argument: it clears and records on
whatever slot is *active* on the selected track, so a run that names a slot without selecting it would report a scope it
never used. The wrapper therefore consumes the runner's `--loop-slot` -- refusing a run that does not name one -- and
presses that slot's own select control before delegating. Without this the leg would not even start: the runner appends
`--loop-slot N` to the legacy arguments and the leg's parser (strict) rejects it.
"""

from __future__ import annotations

import time
from typing import Any

from hitl.scenarios.long_loop_display_window import run_long_loop_display_window
from hitl.verify.windowed_lane import verify_windowed_content_parity

#: The control press the slot selection uses; the leg's own `--press-ms` is its business, this is the wrapper's.
SLOT_SELECT_PRESS_MS = 120


def split_loop_slot_flag(legacy: list[str]) -> tuple[int | None, list[str]]:
    """Take `--loop-slot N` out of the forwarded legacy arguments.

    Returns the named slot (or `None`) and the arguments the reused leg may see. Only the first occurrence is honoured:
    a repeated `--loop-slot` is a caller error and the second value is left for the leg's parser to reject rather than
    silently ignored.
    """
    slot: int | None = None
    remaining: list[str] = []
    index = 0
    while index < len(legacy):
        token = legacy[index]
        if token == "--loop-slot" and index + 1 < len(legacy):
            if slot is None:
                slot = int(legacy[index + 1])
                index += 2
                continue
        remaining.append(token)
        index += 1
    return slot, remaining


def select_loop_slot(out_port: Any, slot: int, *, press_ms: int = SLOT_SELECT_PRESS_MS) -> None:
    """Press the slot's own select control, so the leg's clear/record target is the declared one."""
    from hitl.control_constants import CONTROL_CHANNEL_1BASED, LOOP_SELECT_NOTE_BASE
    from hitl.midi_io import _send_short_press

    _send_short_press(
        out_port,
        note=LOOP_SELECT_NOTE_BASE + (slot - 1),
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
        _caller="windowed_content_parity",
    )


def run_windowed_content_parity(args: object) -> int:
    """Select the declared slot, then run the reused leg unchanged."""
    legacy = list(getattr(args, "legacy_args", []) or [])
    slot, remaining = split_loop_slot_flag(legacy)
    if slot is None:
        print(
            "[windowed-content-parity] --loop-slot is required: the reused leg clears and records on the *active* "
            "slot, so the target must be named and selected rather than inherited from whatever the device had"
        )
        return 2
    if not 1 <= slot <= 8:
        print(f"[windowed-content-parity] --loop-slot {slot} is outside 1..8")
        return 2

    setattr(args, "legacy_args", remaining)

    import mido

    from hitl.midi_io import _find_midi_port

    out_port = mido.open_output(_find_midi_port("Teensy", False))
    try:
        print(f"[windowed-content-parity] selecting loop slot {slot} before the reused leg runs")
        select_loop_slot(out_port, slot)
        time.sleep(0.5)
        return run_long_loop_display_window(args)
    finally:
        out_port.close()


def verify_windowed_content_parity_leg(lines: list[str], args: object) -> dict[str, Any]:
    """The reused leg's oracle **plus** the windowed lane's checks (see `hitl.verify.windowed_lane`)."""
    return verify_windowed_content_parity(lines, args)
