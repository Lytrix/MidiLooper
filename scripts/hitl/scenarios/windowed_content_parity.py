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

from pathlib import Path

from hitl.scenarios.long_loop_display_window import run_long_loop_display_window
from hitl.verify.windowed_lane import verify_windowed_content_parity

#: The control press the slot selection uses; the leg's own `--press-ms` is its business, this is the wrapper's.
SLOT_SELECT_PRESS_MS = 120
#: How long the post-stop drain reads before the run ends. The ring is retained under transport, so the lane's rows
#: produced while a track plays are only delivered once the transport stops and an **idle** flush runs; a run that ends
#: while the transport is still running delivers none of them (measured 2026-10-05).
POST_STOP_DRAIN_S = 12.0
TRANSPORT_STOP_PRESS_MS = 120


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


def last_transport_row_running(lines: list[str]) -> bool | None:
    """Did the last transport edge in this capture leave the transport **running**?

    `None` when the capture carries no transport edge at all. The stop press is only sent when the answer is `True`:
    pressing a *toggle* on a stopped transport would start one, which is the opposite of a drain.
    """
    running: bool | None = None
    for line in lines:
        if "Transport started" in line:
            running = True
        elif "Transport stopped" in line:
            running = False
    return running


def press_global_transport_stop(out_port: Any, *, press_ms: int = TRANSPORT_STOP_PRESS_MS) -> None:
    """Short-press the global transport, which toggles it -- called only when the capture says it is running."""
    from hitl.control_constants import CONTROL_CHANNEL_1BASED, GLOBAL_TRANSPORT_NOTE
    from hitl.midi_io import _send_short_press

    _send_short_press(
        out_port,
        note=GLOBAL_TRANSPORT_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
        _caller="windowed_content_parity",
    )


def serial_port_from(args: object) -> str | None:
    """The serial port this run was given, wherever the runner kept it.

    The layered runner passes `--serial-port` through to the leg's own arguments rather than setting `args.serial_port`,
    so the drain has to look in both places -- otherwise it returns early and the retained rows are never read (found by
    the first run that had a drain to do).
    """
    direct = getattr(args, "serial_port", None)
    if direct:
        return str(direct)
    legacy = list(getattr(args, "legacy_args", []) or [])
    for index, token in enumerate(legacy):
        if token == "--serial-port" and index + 1 < len(legacy):
            return legacy[index + 1]
    return None


def _leg_capture_log(out_dir: Path) -> Path | None:
    logs = sorted(out_dir.glob("host_midi_hitl_long_loop_display_serial_*.log"))
    return logs[-1] if logs else None


def drain_after_transport_stop(args: object, *, drain_s: float = POST_STOP_DRAIN_S) -> Path | None:
    """Stop the transport if the leg left it running, then read until the ring's retained rows are delivered.

    Returns the drain capture's path (`None` when there was nothing to drain, or when the serial port is unknown).
    """
    out_dir = Path(getattr(args, "out_dir", Path("captures")))
    port = serial_port_from(args)
    if not port:
        print("[windowed-content-parity] no serial port on this run: the retained rows cannot be drained")
        return None
    log = _leg_capture_log(out_dir)
    if log is None:
        return None
    lines = log.read_text(encoding="utf-8").splitlines()
    if last_transport_row_running(lines) is not True:
        print("[windowed-content-parity] the capture ends with the transport stopped: nothing retained to drain")
        return None

    import mido

    from hitl.midi_io import _find_midi_port
    from hitl.serial_collector import SerialCaptureCollector

    collector = SerialCaptureCollector(port, 115200)
    collector.start()
    time.sleep(1.0)
    out_port = mido.open_output(_find_midi_port("Teensy", False))
    try:
        print("[windowed-content-parity] stopping the transport so the retained Tier-A rows drain")
        press_global_transport_stop(out_port)
    finally:
        out_port.close()
    time.sleep(drain_s)
    rows = collector.snapshot()
    collector.stop()
    from datetime import datetime

    drain_path = out_dir / f"host_midi_hitl_windowed_post_stop_drain_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
    drain_path.write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"[windowed-content-parity] post-stop drain: {drain_path} ({len(rows)} lines)")
    return drain_path


def run_windowed_content_parity(args: object) -> int:
    """Select the declared slot, run the reused leg, then drain what the transport retained."""
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
    finally:
        out_port.close()

    code = run_long_loop_display_window(args)
    # The lane's rows are produced under transport and the ring is retained while one runs, so the leg's own capture
    # cannot contain them; the drain is what makes them observable at all.
    drain_after_transport_stop(args)
    return code


def verify_windowed_content_parity_leg(lines: list[str], args: object) -> dict[str, Any]:
    """The reused leg's oracle **plus** the windowed lane's checks (see `hitl.verify.windowed_lane`)."""
    return verify_windowed_content_parity(lines, args)
