"""Serial-side transport detection for HITL (Mode A and Mode B capture)."""

from __future__ import annotations

import argparse
from typing import Any, Optional


def serial_follow_active(args: argparse.Namespace) -> bool:
    follow_log = getattr(args, "follow_serial_log", None)
    return bool(getattr(args, "follow_current_session", False) or follow_log)


def serial_capture_active(args: argparse.Namespace) -> bool:
    return bool(getattr(args, "serial_port", None) or serial_follow_active(args))


def parse_latest_bpm(lines: list[str], *, tail: int = 80) -> Optional[float]:
    """Return smoothed BPM from the most recent #CAP,...,BPM line in tail."""
    for line in reversed(lines[-tail:]):
        if "#CAP," not in line or ",BPM," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 5:
            continue
        try:
            raw = float(parts[4])
            smoothed = float(parts[5]) if len(parts) > 5 else raw
        except ValueError:
            continue
        if 20.0 <= smoothed <= 300.0:
            return smoothed
        if 20.0 <= raw <= 300.0:
            return raw
    return None


def serial_lines_show_transport_activity(
    lines: list[str], *, tail: int = 40, after_index: int = 0
) -> bool:
    """True when the current-session suffix shows sequencer activity the device still stands behind.

    Historical lines before ``after_index`` must not authorize a new phase -- and neither may a line
    the ring drained *late*: the firmware writes capture rows through a fixed evidence ring, so an old
    record can land after newer ones and an index window reads it as current
    (`captures/s4f22-step11-h2`: drained `BAR` rows stamped 850-1864 s after a 1855 s row decided
    "transport running", the run skipped its start presses and then pressed a stop onto a stopped
    device, which started it). `filter_live_lines` drops those by the device's own stamp.
    """
    from hitl.device_time import filter_live_lines
    from hitl.serial_collector import serial_line_is_transport

    live = filter_live_lines(lines, after_index=max(after_index, 0))
    return any(serial_line_is_transport(line) for line in live[-tail:])


#: Rows that only a *running* transport emits. `serial_lines_show_transport_activity` answers
#: "is the serial link still alive" and therefore counts `#CAP,DFRAME` -- the periodic
#: display-frame monitor, one row every ~1.5 s whether or not the transport runs. A caller whose
#: question is "is the device's transport running?" must not read a display frame as an answer:
#: on `captures/live-current-root-entry-t8s4-04` the record-arm precondition saw two `DFRAME` rows
#: with the transport stopped, pressed the **toggle** global transport button, *started* the
#: transport (which began the take), and the following "restart transport after arm" press then
#: stopped it -- a 768-tick take with one captured note where the two-bar, eight-note
#: `EDIT_RECORD_FIXTURE` seed was owed.
#:
#: `BAR` is emitted by the firmware when its bar index changes, which only a running sequencer
#: does (a bar realign while stopped can emit one -- a named residual). `BPM` is the device's
#: learned clock rate, emitted only where the session-capture BPM sink is enabled and only while
#: the device is *slaved*; the default `teensy41-capture-serial` build emits none, so `BAR`
#: carries this predicate on the mandated image.
_RUNNING_TRANSPORT_MARKERS = (
    ",BAR,",
    ",BPM,",
)


def serial_lines_show_running_transport(
    lines: list[str], *, tail: int = 40, after_index: int = 0
) -> bool:
    """True only when the live suffix shows rows a *running* transport emits.

    The narrow sibling of :func:`serial_lines_show_transport_activity`: the same device-time
    live window, but `DFRAME`/`MI`/`MO`/`ST` rows do not count. `DFRAME` is emitted while the
    transport is stopped; `MI` is the host's own gestures; `MO` and `ST` are device feedback
    that also flows stopped. Use this for "is the transport running"; use the wider predicate
    when the question is whether the serial link is still alive.
    """
    from hitl.device_time import filter_live_lines

    live = filter_live_lines(lines, after_index=max(after_index, 0))
    return any(
        "#CAP," in line and any(marker in line for marker in _RUNNING_TRANSPORT_MARKERS)
        for line in live[-tail:]
    )


def serial_lines_show_bpm_activity(lines: list[str], *, tail: int = 40) -> bool:
    """Backward-compatible alias: BPM-only activity check."""
    for line in lines[-tail:]:
        if "#CAP," in line and ",BPM," in line:
            return True
    return False


def serial_global_transport_running(lines: list[str], *, after_index: int = 0) -> bool:
    """True when human transport logs show started after the last stop since ``after_index``."""
    started_at = -1
    stopped_at = -1
    for index, line in enumerate(lines):
        if index < after_index:
            continue
        if "Transport started" in line:
            started_at = index
        elif "Transport stopped" in line:
            stopped_at = index
    if started_at >= 0 and started_at > stopped_at:
        return True
    return False


def serial_sequencer_running(
    collector: Any,
    *,
    max_silence_s: float = 4.0,
    after_index: int = 0,
) -> bool:
    """True when capture shows recent sequencer ticks (#CAP,BPM, BAR, DFRAME, etc.).

    Tail activity is checked first: #CAP,DFRAME does not advance the serial heartbeat
    (see serial_line_resets_heartbeat), so a long wall-clock phase can leave
    seconds_since_last_line() stale even while DFRAME lines keep flowing.
    """
    lines = collector.snapshot()
    if serial_lines_show_transport_activity(lines, after_index=after_index):
        return True
    if after_index > 0:
        return False
    transport_age = getattr(collector, "seconds_since_last_transport_line", lambda: None)()
    if transport_age is not None and transport_age <= max_silence_s:
        return True
    return False


def use_serial_transport_proxy(
    args: argparse.Namespace,
    serial_collector: Any | None,
    *,
    after_index: int = 0,
) -> bool:
    if serial_collector is None or not serial_capture_active(args):
        return False
    # A run whose transport is already known running does not need the clock fallback at all,
    # and letting it fire would press Play/Stop (a toggle) and interrupt an active overdub.
    # An explicit current-session transport edge is stronger evidence than a short tail that
    # may be filled with note or display records. The device *does* emit USB MIDI clock
    # (`hitl/midi_clock.py`); this guard is about not toggling, not about the clock's absence.
    lines = serial_collector.snapshot()
    if serial_global_transport_running(lines, after_index=after_index):
        return True
    return serial_sequencer_running(serial_collector, after_index=after_index)


def resolve_wall_tempo_bpm(serial_collector: Any | None, fallback_bpm: float) -> float:
    """Prefer BPM from serial capture tail; else use CLI fallback."""
    if serial_collector is not None:
        bpm = parse_latest_bpm(serial_collector.snapshot())
        if bpm is not None:
            return bpm
    return fallback_bpm


def resolve_wall_tempo_for_proxy(
    serial_collector: Any | None,
    args: argparse.Namespace,
    *,
    using_serial_proxy: bool,
) -> Optional[float]:
    if not using_serial_proxy:
        return None
    fallback = float(getattr(args, "tempo_bpm", 120.0) or 120.0)
    return resolve_wall_tempo_bpm(serial_collector, fallback)
