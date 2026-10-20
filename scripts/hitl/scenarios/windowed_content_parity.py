"""Task 6.4's windowed leg: the existing long-loop content-verification leg, read for the windowed lane.

The design's own rule is that F-7/F-8 *feed* the content-verification pipeline rather than building a second one
(`design.md`: "fed the windowed/demoted cases (new stimulus profile), not replaced"). So this module adds no driver of
its own: it reuses `run_long_loop_display_window` -- a leg that records a loop far longer than the served window
(`--record-bars`, default 24, and its own run refuses <= 16), keeps the transport running, and already has a host-tested
verifier -- and pairs it with the windowed lane's own checks (`hitl.verify.windowed_lane`).

**The three things this wrapper owns are the target, the demand and the drain.** The reused leg takes no slot
argument: it clears and records on whatever slot is *active* on the selected track, so a run that names a slot without
selecting it would report a scope it never used. The wrapper therefore consumes the runner's `--loop-slot` -- refusing a
run that does not name one -- and presses that slot's own select control before delegating. **The demand** is
`--switch-target-slot`: a switch to a filled, demoted target is what makes the launch bind a first window and so gives
the window lane something to roll from (a plain record-and-play owes none), and `--switch-settle-s` is the time that
demand is given to be served. Without the slot press the leg would not even start: the runner appends `--loop-slot N` to
the legacy arguments, and the leg's own parser is strict about arguments it does not know.
"""

from __future__ import annotations

import json
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any
from hitl.boot_handoff import wait_for_boot_handoff
from hitl.scenarios.long_loop_display_window import run_long_loop_display_window
from hitl.verify.windowed_lane import verify_windowed_content_parity

#: The control press the slot selection uses; the leg's own `--press-ms` is its business, this is the wrapper's.
SLOT_SELECT_PRESS_MS = 120
#: How long the post-stop drain reads before the run ends. The ring is retained under transport, so the lane's rows
#: produced while a track plays are only delivered once the transport stops and an **idle** flush runs; a run that ends
#: while the transport is still running delivers none of them (measured 2026-10-05).
POST_STOP_DRAIN_S = 12.0
TRANSPORT_STOP_PRESS_MS = 120
#: How long the switch leg waits for the queued switch's target to be prepared **and for the demand it creates to be
#: served**, before the drain. The wait is what makes the lane's rows exist at all: the switch's launch binds the
#: target's first window, and only then can the rolling lane roll forward from it.
#:
#: The earlier 45 s wait was based on a rootless-target publication needing well over 30 s at the measured ~105 eligible
#: turns/s x 2 units (`captures/s64-progress-01`). An aligned 24-bar run with 90 s still observed only one publication and
#: a Tier-A eviction, so this is a longer opportunity, not a timing guarantee; the verifier requires repeated builds and
#: intact ring counters. Callers may override `--switch-settle-s` for a bounded experiment.
SWITCH_SETTLE_S = 90.0
#: The edit control (note 38) held long enough to **exit** the loop-edit session a slot selection opens. Measured on
#: device (run 04): selecting the target enters `LOOP_EDIT`, and while that session is open the queued switch's
#: preparation is never reached -- no provision row, no prewarm, no lane work -- so the switch holds until its bound.
#: The firmware's own long-press window is 350 ms (`EDIT_BUTTON_DEBOUNCE_MS`); 700 ms is the value the reused leg uses.
EDIT_EXIT_PRESS_MS = 700


def split_flag(legacy: list[str], name: str) -> tuple[int | None, list[str]]:
    """Take `<name> N` out of the legacy arguments, returning the value and what the reused leg may see.

    The wrapper owns flags the reused leg has no argument for (`--loop-slot`, `--switch-target-slot`,
    `--switch-settle-s`); forwarding one would abort the leg's **strict** parser before it drives anything -- which is
    exactly how the switch leg failed its first attempt. Only the first occurrence is honoured: a repeat is a caller
    error and is left for the leg to reject rather than silently ignored. (This helper takes integer-valued flags; the
    settle window is a float and has its own splitter, `split_switch_settle_flag`.)
    """
    value: int | None = None
    remaining: list[str] = []
    index = 0
    while index < len(legacy):
        token = legacy[index]
        if token == name and index + 1 < len(legacy):
            if value is None:
                value = int(legacy[index + 1])
                index += 2
                continue
        remaining.append(token)
        index += 1
    return value, remaining


def split_loop_slot_flag(legacy: list[str]) -> tuple[int | None, list[str]]:
    """Take `--loop-slot N` out of the forwarded legacy arguments."""
    return split_flag(legacy, "--loop-slot")


def split_switch_target_slot_flag(legacy: list[str]) -> tuple[int | None, list[str]]:
    """Take `--switch-target-slot N` out of the forwarded legacy arguments."""
    return split_flag(legacy, "--switch-target-slot")


def split_switch_settle_flag(legacy: list[str]) -> tuple[float | None, list[str]]:
    """Take `--switch-settle-s SECONDS` out of the forwarded legacy arguments.

    A **float**, unlike the slot flags, and an unparsable value is refused **with the token named** rather than left
    in the argument list: the reused leg has no such flag, so leaving it there would abort that leg's strict parser
    and blame the leg for the wrapper's own flag (the failure mode the switch flag names).
    """
    value: float | None = None
    remaining: list[str] = []
    index = 0
    while index < len(legacy):
        token = legacy[index]
        if token == "--switch-settle-s" and index + 1 < len(legacy):
            if value is None:
                raw = legacy[index + 1]
                try:
                    value = float(raw)
                except ValueError as error:
                    raise ValueError(f"--switch-settle-s {raw!r} is not a number of seconds") from error
                index += 2
                continue
        remaining.append(token)
        index += 1
    return value, remaining


def split_post_stop_start_flag(legacy: list[str]) -> tuple[float | None, list[str]]:
    """Take `--post-stop-start-s SECONDS` out of the forwarded legacy arguments.

    The replay leg exists because the demand's own entry state -- playing, **no record-return window bound**, published
    root current -- is only reached by a transport start that happens *after* a stop: the record-return window retires
    with the stop, so a start during the take's own record leg finds the coverage role still bound (`rw=1`) and the lane
    can only report `win_blocked`/`win_busy`. A float, refused with the token named, for the same reason as
    `--switch-settle-s`.
    """
    value: float | None = None
    remaining: list[str] = []
    index = 0
    while index < len(legacy):
        token = legacy[index]
        if token == "--post-stop-start-s" and index + 1 < len(legacy):
            if value is None:
                raw = legacy[index + 1]
                try:
                    value = float(raw)
                except ValueError as error:
                    raise ValueError(f"--post-stop-start-s {raw!r} is not a number of seconds") from error
                index += 2
                continue
        remaining.append(token)
        index += 1
    return value, remaining


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


def _press_global_transport(out_port: Any, *, press_ms: int) -> None:
    from hitl.control_constants import CONTROL_CHANNEL_1BASED, GLOBAL_TRANSPORT_NOTE
    from hitl.midi_io import _send_short_press

    _send_short_press(
        out_port,
        note=GLOBAL_TRANSPORT_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
        _caller="windowed_content_parity",
    )


def press_global_transport_stop(out_port: Any, *, press_ms: int = TRANSPORT_STOP_PRESS_MS) -> None:
    """Short-press the global transport, which toggles it -- called only when the capture says it is running."""
    _press_global_transport(out_port, press_ms=press_ms)


def press_global_transport_start(out_port: Any, *, press_ms: int = TRANSPORT_STOP_PRESS_MS) -> None:
    """Short-press the global transport when the capture says it is **stopped**, which starts it.

    The control is a toggle. The direction is named separately from `press_global_transport_stop` so a caller cannot
    swap them by accident: this one is only ever called with a capture whose last edge is `Transport stopped`.
    """
    _press_global_transport(out_port, press_ms=press_ms)


def switch_target_slot_from(legacy: list[str]) -> int | None:
    """The filled, non-active slot the switch leg should switch *to*, or `None` for the base leg only.

    This is what gives the window lane a **demand** under transport: a plain record-and-play owes no window (measured),
    while a switching target owes one because the launch prepares it. Absent by default, so the base behaviour is
    unchanged and the two shapes stay comparable.
    """
    return split_switch_target_slot_flag(legacy)[0]


def press_edit_exit(out_port: Any, *, press_ms: int = EDIT_EXIT_PRESS_MS) -> None:
    """Hold the edit control long enough to leave the loop-edit session a slot selection opens.

    Measured, not assumed: run 04 selected the target, the device entered `LOOP_EDIT`, and the queued switch then sat
    untouched for its whole bound — no provision row, no capture prewarm, and no lane work — while the lane refuses
    while an edit session is active. The S4b device legs prepared their switches with no such session open.
    """
    from hitl.control_constants import CONTROL_CHANNEL_1BASED, EDIT_BUTTON_NOTE
    from hitl.midi_io import _send_short_press

    _send_short_press(
        out_port,
        note=EDIT_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
        _caller="windowed_content_parity",
    )


def switch_leg(out_port: Any, target_slot: int, *, settle_s: float = SWITCH_SETTLE_S) -> None:
    """Select the target, leave the edit session that opens, then wait for the switch the selection queued.

    **The selection is the whole gesture.** A short press on a *non-selected filled* slot while the track plays is the
    firmware's queue-playback path (`MidiButtonActions`: "queue playback, never overdub"), and the device logs
    `Loop N: Queued playback switch` before any other control is touched. The target must therefore be filled: an empty
    one takes the record-arm branch instead.

    The leg used to follow the selection with a press of the **global** record control, on the assumption that it acts on
    the selected slot. It does not: it acts on the active track and starts a live overdub there (device: `Live Overdub`
    1.8 s after the switch was already queued), and the commit ladder runs only `if (tracks[i].isPlaying())`, so the
    queued switch could not commit for the whole settle in every run 06/07/09. The press is gone; the switch is queued
    before it ever happened.

    The **edit exit** stays: the selection opens a session the switch's preparation does not survive (run 04).
    """
    if not 1 <= target_slot <= 8:
        raise ValueError(f"switch target slot {target_slot} is outside 1..8")
    select_loop_slot(out_port, target_slot)
    time.sleep(0.5)
    press_edit_exit(out_port)
    time.sleep(settle_s)


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

def _capture_session_log(out_dir: Path) -> Path | None:
    """The **managed capture's** own session log -- the file still being appended when the leg has already returned.

    The leg's `--serial-port` view copies the capture's stream into its own log and stops writing when the leg returns,
    so a post-leg drain that read *that* file would read a file nobody is writing to any more. The capture names its
    live log in `.current_session` (a repo-relative path); the glob is the fallback for a directory whose pointer is
    gone.
    """
    pointer = out_dir / ".current_session"
    if pointer.is_file():
        try:
            named = pointer.read_text(encoding="utf-8").strip()
        except OSError:
            named = ""
        for candidate in (Path(named), out_dir / named):
            if named and candidate.is_file():
                return candidate
    logs = sorted(out_dir.glob("session_*.log"))
    return logs[-1] if logs else None


def run_post_leg_phase(
    args: object,
    *,
    switch_target_slot: int | None = None,
    drain_s: float = POST_STOP_DRAIN_S,
    switch_settle_s: float = SWITCH_SETTLE_S,
    post_stop_start_s: float | None = None,
) -> Path | None:
    """Everything the driver owes **after** the reused leg: the switch leg (optional), then the stop-and-drain.

    Both halves are gated on the transport actually running, for the same reason: a switch or a drain performed with the
    transport stopped is not the regime this leg exists for (the lane's rows are produced *under transport*), and the stop
    press is a toggle that would otherwise **start** a transport.
    """
    out_dir = Path(getattr(args, "out_dir", Path("captures")))
    port = serial_port_from(args)
    capture_log = _capture_session_log(out_dir)
    edge_log = _leg_capture_log(out_dir) or capture_log
    if edge_log is None and port is None:
        print("[windowed-content-parity] neither a serial port nor a capture log on this run: the retained rows cannot "
              "be drained")
        return None
    transport_running: bool | None = None
    if edge_log is not None:
        lines = edge_log.read_text(encoding="utf-8").splitlines()
        transport_running = last_transport_row_running(lines)
        if transport_running is not True and post_stop_start_s is None:
            print("[windowed-content-parity] the capture ends with the transport stopped: neither a switch leg nor a "
                  "drain belongs here")
            return None
        if transport_running is True and post_stop_start_s is not None:
            # The replay leg starts a **stopped** transport: with the transport already running there is nothing to
            # replay, and pressing the toggle would stop it (the opposite of the leg's purpose).
            print("[windowed-content-parity] --post-stop-start-s names a start for a stopped transport, but this "
                  "capture ends with it running")
            return None

    import mido

    from hitl.midi_io import _find_midi_port

    collector = None
    drain_log: Path | None = None
    drain_from = 0
    if port:
        from hitl.serial_collector import SerialCaptureCollector

        collector = SerialCaptureCollector(port, 115200)
        collector.start()
        time.sleep(1.0)
    else:
        # The managed capture owns the USB port, and a second owner of one port is a broken run. Its session log is
        # still being appended, so the drain reads **its** file: the retained rows are the ones it adds after the stop
        # press. (The leg's own copy stops at leg exit, so it cannot carry the drain.)
        drain_log = capture_log
        if drain_log is None:
            print("[windowed-content-parity] the capture owns the serial port but no session log names it: the retained "
                  "rows cannot be drained")
            return None
        print(f"[windowed-content-parity] the capture owns the serial port: draining {drain_log.name}")
        drain_from = len(drain_log.read_text(encoding="utf-8").splitlines())

    # The post-leg switch/stop gestures must not land inside the device's boot/display handoff
    # (owner report 2026-10-12). Row evidence, not a sleep; no collector (managed capture owns the
    # port) -> skip.
    if collector is not None:
        handoff = wait_for_boot_handoff(collector)
        print(f"[hitl] boot handoff: {handoff}")
        if handoff == "timeout":
            print(
                "[error] device boot handoff not observed within 30.0s (DisplayManager: Boot "
                "setup complete / boot_commit missing): refusing to send control gestures into "
                "an unfinished boot",
                file=sys.stderr,
            )
            raise SystemExit(3)
    out_port = mido.open_output(_find_midi_port("Teensy", False))
    try:
        if transport_running is False and post_stop_start_s is not None:
            print("[windowed-content-parity] replay leg: the capture ends with the transport stopped; pressing the "
                  f"transport once and settling {post_stop_start_s:.1f}s with the take in place "
                  "(the record-return window retires with the stop, which is the demand's own entry state)")
            press_global_transport_start(out_port)
            time.sleep(post_stop_start_s)
        if switch_target_slot is not None:
            if not 1 <= switch_target_slot <= 8:
                print(f"[windowed-content-parity] --switch-target-slot {switch_target_slot} is outside 1..8")
                return None
            print(f"[windowed-content-parity] switch leg: selecting slot {switch_target_slot} to queue a switch on the "
                  f"selection itself while the transport runs")
            switch_leg(out_port, switch_target_slot, settle_s=switch_settle_s)

        print("[windowed-content-parity] stopping the transport so the retained Tier-A rows drain")
        press_global_transport_stop(out_port)
    finally:
        out_port.close()
    time.sleep(drain_s)
    if collector is not None:
        rows = collector.snapshot()
        collector.stop()
    else:
        rows = drain_log.read_text(encoding="utf-8").splitlines()[drain_from:] if drain_log is not None else []
    from datetime import datetime

    drain_path = out_dir / f"host_midi_hitl_windowed_post_stop_drain_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
    drain_path.write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"[windowed-content-parity] post-stop drain: {drain_path} ({len(rows)} lines)")
    return drain_path


def run_windowed_content_parity(args: object) -> int:
    """Select the declared slot, run the reused leg, then drive the demand (switch) and drain what the transport kept.

    The wrapper owns three flags the reused leg has no argument for -- `--loop-slot`, `--switch-target-slot` and
    `--switch-settle-s` -- and refuses a run that names them badly **before** any device contact, because a flag left
    in the forwarded arguments aborts the leg's strict parser and blames the leg.
    """
    legacy = list(getattr(args, "legacy_args", []) or [])
    slot, remaining = split_loop_slot_flag(legacy)
    switch_target, remaining = split_switch_target_slot_flag(remaining)
    try:
        switch_settle, remaining = split_switch_settle_flag(remaining)
        post_stop_start, remaining = split_post_stop_start_flag(remaining)
    except ValueError as error:
        print(f"[windowed-content-parity] {error}")
        return 2
    if switch_settle is not None and not switch_settle > 0:
        print(f"[windowed-content-parity] --switch-settle-s {switch_settle} must be a positive number of seconds")
        return 2
    if post_stop_start is not None and not post_stop_start > 0:
        print(f"[windowed-content-parity] --post-stop-start-s {post_stop_start} must be a positive number of seconds")
        return 2
    if switch_settle is not None and switch_target is None:
        # The wait belongs to the switch leg: without a target there is no switch, so a run that names a settle would
        # silently measure nothing and could still read as "the lane was not asked".
        print("[windowed-content-parity] --switch-settle-s has no meaning without --switch-target-slot: the wait is "
              "the time the switch's own demand gets to be served")
        return 2
    if slot is None:
        print(
            "[windowed-content-parity] --loop-slot is required: the reused leg clears and records on the *active* "
            "slot, so the target must be named and selected rather than inherited from whatever the device had"
        )
        return 2
    if not 1 <= slot <= 8:
        print(f"[windowed-content-parity] --loop-slot {slot} is outside 1..8")
        return 2

    setattr(args, "windowed_record_slot", slot)
    setattr(args, "windowed_switch_target_slot", switch_target)
    setattr(args, "legacy_args", remaining)
    ring_stats_before = _request_ring_stats_readback("before-record")

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
    drain_path = run_post_leg_phase(
        args,
        switch_target_slot=switch_target,
        switch_settle_s=switch_settle if switch_settle is not None else SWITCH_SETTLE_S,
        post_stop_start_s=post_stop_start,
    )
    ring_stats_after = _request_ring_stats_readback("after-stop-drain")
    out_dir = Path(getattr(args, "out_dir", Path("captures")))
    capture_log = _capture_session_log(out_dir)
    if capture_log is not None:
        verification_lines = capture_log.read_text(encoding="utf-8", errors="replace").splitlines()
    else:
        leg_log = _leg_capture_log(out_dir)
        verification_lines = leg_log.read_text(encoding="utf-8", errors="replace").splitlines() if leg_log else []
        if drain_path is not None and drain_path.is_file():
            verification_lines.extend(drain_path.read_text(encoding="utf-8", errors="replace").splitlines())

    leg_reports = sorted(out_dir.glob("host_midi_hitl_long_loop_display_window_*.json"))
    if leg_reports:
        try:
            standalone = json.loads(leg_reports[-1].read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            standalone = {}
        if isinstance(standalone.get("serial_verification"), dict):
            setattr(args, "windowed_leg_run_report", standalone["serial_verification"])
    report = verify_windowed_content_parity_leg(verification_lines, args)
    report["run"] = {
        "leg_exit_code": code,
        "record_slot": slot,
        "switch_target_slot": switch_target,
        "verification_rows": len(verification_lines),
        "ring_stats_before": ring_stats_before,
        "ring_stats_after": ring_stats_after,
    }
    if leg_reports:
        report["run"]["standalone_leg_report"] = str(leg_reports[-1])
    if code != 0:
        report["ok"] = False
        detail = f"execution: reused long-loop leg exited {code}"
        if leg_reports:
            detail += f"; standalone leg report={leg_reports[-1]}"
        report["issues"].append(detail)
    out_dir.mkdir(parents=True, exist_ok=True)
    report_path = out_dir / f"host_midi_hitl_windowed_content_parity_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"[windowed-content-parity] combined verifier report: {report_path}")
    if not report["ok"]:
        for issue in report["issues"]:
            print(f"[windowed-content-parity] verifier: {issue}")
    return max(code, 0 if report["ok"] else 2)


def verify_windowed_content_parity_leg(lines: list[str], args: object) -> dict[str, Any]:
    """The reused leg's oracle **plus** the windowed lane's checks (see `hitl.verify.windowed_lane`)."""
    return verify_windowed_content_parity(lines, args)
def _request_ring_stats_readback(stage: str) -> bool:
    """Use the managed serial owner for a stopped-only, read-only ring counter snapshot."""
    try:
        from hitl.managed_capture import active_managed_capture
    except ImportError:
        return False
    session = active_managed_capture()
    if session is None:
        print(f"[windowed-content-parity] {stage} RING stats unavailable: no managed capture owner")
        return False
    rows = session.identity_stat_lines(timeout_s=6.0)
    if rows is None:
        print(f"[windowed-content-parity] {stage} RING stats readback unavailable")
        return False
    print(f"[windowed-content-parity] {stage} RING stats readback received")
    return True
