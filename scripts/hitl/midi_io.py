"""MIDI port discovery and short-press helpers for HITL."""

from __future__ import annotations

import inspect
import time

try:
    import mido
except ImportError as exc:  # pragma: no cover - import guard
    raise SystemExit(
        "Missing dependency 'mido'. Install with:\n"
        "  python3 -m pip install mido python-rtmidi pyserial"
    ) from exc


def _find_midi_port(name_substring: str, is_input: bool, timeout_s: float = 5.0) -> str:
    lowered = name_substring.lower()
    deadline = time.monotonic() + max(timeout_s, 0.0)
    names: list[str] = []
    while True:
        names = mido.get_input_names() if is_input else mido.get_output_names()
        for name in names:
            if lowered in name.lower():
                return name
        if time.monotonic() >= deadline:
            break
        time.sleep(0.1)
    role = "input" if is_input else "output"
    available = "\n".join(f"  - {n}" for n in names) or "  (none)"
    raise RuntimeError(
        f"No MIDI {role} port matching '{name_substring}'.\nAvailable {role} ports:\n{available}"
    )


def _send_long_press(
    out_port: mido.ports.BaseOutput, *, note: int, channel_1based: int, press_ms: int
) -> None:
    """Hold a control-surface note for long-press actions (clear, etc.)."""
    ch = channel_1based - 1
    out_port.send(mido.Message("note_on", channel=ch, note=note, velocity=127))
    time.sleep(max(press_ms, 1) / 1000.0)
    out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))


def _record_control_press(action: str, *, note: int, channel_1based: int, caller: str) -> None:
    """Record this press in the shared control-action ledger (never fails a run)."""
    try:
        from hitl.transport_clock import record_control_action
    except Exception:  # pragma: no cover - the ledger is diagnostics only
        return
    record_control_action(action, note=note, channel_1based=channel_1based, caller=caller)


def _send_short_press(
    out_port: mido.ports.BaseOutput, *, note: int, channel_1based: int, press_ms: int,
    _caller: str | None = None,
) -> None:
    # The baseline's own presses belong in the same ledger as the clock helper's, so an
    # unexpected transport press is attributable to its caller from the run report rather than
    # inferred from the device's reaction. A multi-press passes its own caller down, so each
    # constituent press is recorded once and attributed to the code that asked for the gesture.
    _record_control_press(
        "short_press", note=note, channel_1based=channel_1based,
        caller=_caller or inspect.stack()[1].function,
    )
    _send_long_press(
        out_port, note=note, channel_1based=channel_1based, press_ms=press_ms
    )


def _send_multi_short_press(
    out_port: mido.ports.BaseOutput,
    *,
    note: int,
    channel_1based: int,
    press_ms: int,
    count: int,
    gap_ms: int = 80,
) -> None:
    caller = inspect.stack()[1].function
    for i in range(max(count, 0)):
        # Each constituent press is the outgoing action; the composite is its composition, not a
        # separate press, so recording both would double-count transport presses.
        _send_short_press(
            out_port,
            note=note,
            channel_1based=channel_1based,
            press_ms=press_ms,
            _caller=caller,
        )
        if i + 1 < count:
            time.sleep(max(gap_ms, 1) / 1000.0)


def _drain_input_messages(in_port: mido.ports.BaseInput) -> int:
    count = 0
    while True:
        msg = in_port.poll()
        if msg is None:
            break
        count += 1
    return count
