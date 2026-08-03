"""Edit-mode MIDI press helpers and transport start/stop for HITL."""

from __future__ import annotations

import time
from typing import Any

try:
    import mido
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "Missing dependency 'mido'. Install with:\n"
        "  python3 -m pip install mido python-rtmidi pyserial"
    ) from exc

from hitl.control_constants import CONTROL_CHANNEL_1BASED, GLOBAL_TRANSPORT_NOTE
from hitl.midi_io import _send_multi_short_press, _send_short_press
from hitl.transport_clock import clock_seen_within


def _send_long_press(
    out_port: mido.ports.BaseOutput, *, note: int, channel_1based: int, press_ms: int
) -> None:
    ch = channel_1based - 1
    out_port.send(mido.Message("note_on", channel=ch, note=note, velocity=127))
    time.sleep(max(press_ms, 1) / 1000.0)
    out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))


def _send_double_press(
    out_port: mido.ports.BaseOutput,
    *,
    note: int,
    channel_1based: int,
    press_ms: int,
    gap_ms: int = 80,
) -> None:
    _send_multi_short_press(
        out_port,
        note=note,
        channel_1based=channel_1based,
        press_ms=press_ms,
        count=2,
        gap_ms=gap_ms,
    )


def _send_triple_press(
    out_port: mido.ports.BaseOutput,
    *,
    note: int,
    channel_1based: int,
    press_ms: int,
    gap_ms: int = 80,
) -> None:
    _send_multi_short_press(
        out_port,
        note=note,
        channel_1based=channel_1based,
        press_ms=press_ms,
        count=3,
        gap_ms=gap_ms,
    )


def _ensure_transport_running(
    out_port: mido.ports.BaseOutput,
    in_port: mido.ports.BaseInput,
    *,
    press_ms: int,
    phase_wait_ms: int,
    serial_collector: Any | None = None,
    transport_args: Any | None = None,
) -> bool:
    """Start transport when USB MIDI clock is absent and serial proxy is not active."""
    from hitl.serial_transport import use_serial_transport_proxy

    if transport_args is not None and use_serial_transport_proxy(transport_args, serial_collector):
        return True
    if clock_seen_within(in_port, 0.5):
        return True
    for attempt in range(1, 4):
        _send_short_press(
            out_port,
            note=GLOBAL_TRANSPORT_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=press_ms,
        )
        time.sleep(phase_wait_ms / 1000.0)
        if clock_seen_within(in_port, 1.0):
            return True
        print(f"[warn] No MIDI clock after transport start (attempt {attempt}/3)")
    return clock_seen_within(in_port, 1.0)


def _stop_transport_if_running(
    out_port: mido.ports.BaseOutput,
    in_port: mido.ports.BaseInput,
    *,
    press_ms: int,
    phase_wait_ms: int,
) -> bool:
    """Toggle transport off when the host already sees MIDI clock."""
    if not clock_seen_within(in_port, 0.5):
        return False
    print("[edit-hitl] transport stop before clear/record precondition")
    _send_short_press(
        out_port,
        note=GLOBAL_TRANSPORT_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    time.sleep(phase_wait_ms / 1000.0)
    return True
