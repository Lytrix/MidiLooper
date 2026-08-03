"""Shared MIDI + serial helpers for load/save overlay HITL (watchable on hardware OLED)."""

from __future__ import annotations

import argparse
import re
import time
from typing import Optional

from hitl.verify.load_save_display import last_load_save_mode_active

ROOT_FIRST_SET_ROW = 2
DIRTY_PROMPT_ROW = {"yes": 0, "no": 1, "cancel": 2}

RECORD_SCROLL_NOTE = 36
TRACK_SCROLL_NOTE = 37

def overlay_scroll_steps_to_set_row(catalog_set_index: int) -> int:
    return ROOT_FIRST_SET_ROW + max(catalog_set_index, 0)

def parse_overlay_watch_args(args: object) -> argparse.Namespace:
    defaults = {
        "overlay_scroll_step_dwell_ms": 3000,
        "overlay_root_dwell_ms": 5000,
        "overlay_dirty_dwell_ms": 3000,
        "catalog_set_index": 0,
    }
    for key, default in defaults.items():
        if hasattr(args, key):
            defaults[key] = getattr(args, key)
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--track-number", "--track", type=int, default=5)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--double-press-gap-ms", type=int, default=80)
    parser.add_argument("--gesture-settle-ms", type=int, default=600)
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--catalog-set-index", type=int, default=defaults["catalog_set_index"])
    parser.add_argument(
        "--overlay-scroll-step-dwell-ms",
        type=int,
        default=defaults["overlay_scroll_step_dwell_ms"],
        help="Pause on each overlay list row after scroll (watch on OLED)",
    )
    parser.add_argument(
        "--overlay-root-dwell-ms",
        type=int,
        default=defaults["overlay_root_dwell_ms"],
        help="Pause with root overlay open before exit",
    )
    parser.add_argument(
        "--overlay-dirty-dwell-ms",
        type=int,
        default=defaults["overlay_dirty_dwell_ms"],
        help="Pause on dirty-prompt row before confirm",
    )
    legacy = list(getattr(args, "legacy_args", []) or [])
    known, _unknown = parser.parse_known_args(legacy)
    return known

def dwell_ms(ms: int, log_prefix: str, label: str) -> None:
    if ms <= 0:
        return
    print(f"{log_prefix} >>> dwell {ms}ms — {label} <<<")
    time.sleep(ms / 1000.0)

def wait_load_save_mode(serial_collector: object, expected: int, timeout_ms: int) -> bool:
    deadline = time.time() + timeout_ms / 1000.0
    while time.time() < deadline:
        if last_load_save_mode_active(serial_collector.snapshot()) == expected:
            return True
        time.sleep(0.05)
    return last_load_save_mode_active(serial_collector.snapshot()) == expected

def wait_serial_cap_ready(serial_collector: object, timeout_ms: int = 8000) -> bool:
    deadline = time.time() + timeout_ms / 1000.0
    while time.time() < deadline:
        for line in serial_collector.snapshot():
            if "#CAP," in line:
                return True
        time.sleep(0.05)
    return False

def wait_overlay_selection(
    serial_collector: object,
    *,
    mode: int,
    row: int,
    after_line_index: int,
    timeout_s: float,
    log_prefix: str,
) -> bool:
    deadline = time.time() + timeout_s
    pattern = re.compile(rf"#CAP,\d+,OVLY,sel,{mode},{row}\b")
    while time.time() < deadline:
        lines = serial_collector.snapshot()
        for line in lines[after_line_index:]:
            if pattern.search(line):
                return True
        time.sleep(0.05)
    print(f"{log_prefix} warn: timed out waiting for OVLY,sel,{mode},{row}")
    return False

def enter_load_save_overlay(
    out_port: object,
    serial_collector: object,
    *,
    press_ms: int,
    gap_ms: int,
    gesture_settle_ms: int,
    send_double_press: object,
    log_prefix: str,
) -> bool:
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_NOTE,
        PLAY_STOP_BUTTON_NOTE,
    )

    cap_ready = wait_serial_cap_ready(serial_collector, timeout_ms=5000)
    for attempt, note in enumerate((EDIT_BUTTON_NOTE, PLAY_STOP_BUTTON_NOTE), start=1):
        label = "edit" if note == EDIT_BUTTON_NOTE else "play/stop"
        print(f"{log_prefix} >>> {label} double-press — enter load/save overlay (try {attempt}) <<<")
        send_double_press(
            out_port,
            note=note,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=press_ms,
            gap_ms=gap_ms,
        )
        time.sleep(gesture_settle_ms / 1000.0)
        if not cap_ready:
            print(f"{log_prefix} warn: serial CAP unavailable — proceeding after gesture")
            return True
        if wait_load_save_mode(serial_collector, 1, max(gesture_settle_ms, 1500)):
            print(f"{log_prefix} overlay open (LDSV=1)")
            return True
    print(f"{log_prefix} warn: LDSV not verified — proceeding after enter gestures")
    return True

def exit_load_save_overlay(
    out_port: object,
    serial_collector: object,
    *,
    press_ms: int,
    gap_ms: int,
    gesture_settle_ms: int,
    send_double_press: object,
    log_prefix: str,
) -> None:
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_NOTE,
        PLAY_STOP_BUTTON_NOTE,
    )

    if last_load_save_mode_active(serial_collector.snapshot()) != 1:
        return
    for note, label in ((EDIT_BUTTON_NOTE, "edit"), (PLAY_STOP_BUTTON_NOTE, "play/stop")):
        print(f"{log_prefix} >>> {label} double-press — exit load/save overlay <<<")
        send_double_press(
            out_port,
            note=note,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=press_ms,
            gap_ms=gap_ms,
        )
        time.sleep(gesture_settle_ms / 1000.0)
        if wait_load_save_mode(serial_collector, 0, max(gesture_settle_ms, 1500)):
            print(f"{log_prefix} overlay closed (LDSV=0)")
            return

def recover_load_save_overlay(
    out_port: object,
    serial_collector: object,
    *,
    press_ms: int,
    gap_ms: int,
    gesture_settle_ms: int,
    send_double_press: object,
    log_prefix: str,
) -> None:
    """Close dirty prompt or open overlay so a fresh enter can run."""
    if last_load_save_mode_active(serial_collector.snapshot()) == 1:
        exit_load_save_overlay(
            out_port,
            serial_collector,
            press_ms=press_ms,
            gap_ms=gap_ms,
            gesture_settle_ms=gesture_settle_ms,
            send_double_press=send_double_press,
            log_prefix=log_prefix,
        )

def midi_scroll_overlay(
    out_port: object,
    *,
    steps: int,
    direction: str,
    channel_1based: int,
    press_ms: int,
    phase_wait_ms: int,
    send_short_press: object,
    log_prefix: str,
    row_label: str,
) -> None:
    if steps <= 0:
        return
    note = RECORD_SCROLL_NOTE if direction == "down" else TRACK_SCROLL_NOTE
    verb = "down" if direction == "down" else "up"
    print(
        f"{log_prefix} >>> {'record' if direction == 'down' else 'track'} short x{steps} "
        f"— scroll {verb} to {row_label} <<<"
    )
    for _ in range(steps):
        send_short_press(
            out_port,
            note=note,
            channel_1based=channel_1based,
            press_ms=press_ms,
        )
        time.sleep(phase_wait_ms / 1000.0)

def midi_scroll_to_root_row(
    out_port: object,
    serial_collector: object,
    *,
    target_row: int,
    channel_1based: int,
    press_ms: int,
    phase_wait_ms: int,
    step_dwell_ms: int,
    send_short_press: object,
    log_prefix: str,
) -> int:
    """Scroll from Save (row 0) down to target_row. Returns serial anchor after scroll."""
    anchor = len(serial_collector.snapshot())
    midi_scroll_overlay(
        out_port,
        steps=target_row,
        direction="down",
        channel_1based=channel_1based,
        press_ms=press_ms,
        phase_wait_ms=phase_wait_ms,
        send_short_press=send_short_press,
        log_prefix=log_prefix,
        row_label=f"root row {target_row}",
    )
    if wait_serial_cap_ready(serial_collector, timeout_ms=500):
        wait_overlay_selection(
            serial_collector,
            mode=0,
            row=target_row,
            after_line_index=anchor,
            timeout_s=5.0,
            log_prefix=log_prefix,
        )
    dwell_ms(step_dwell_ms, log_prefix, f"root row {target_row}")
    return anchor

def midi_confirm_overlay_row(
    out_port: object,
    *,
    channel_1based: int,
    press_ms: int,
    gesture_settle_ms: int,
    send_short_press: object,
    log_prefix: str,
    action_label: str,
) -> None:
    from hitl.control_constants import EDIT_BUTTON_NOTE

    print(f"{log_prefix} >>> edit short — {action_label} <<<")
    send_short_press(
        out_port,
        note=EDIT_BUTTON_NOTE,
        channel_1based=channel_1based,
        press_ms=press_ms,
    )
    time.sleep(gesture_settle_ms / 1000.0)

def midi_scroll_dirty_prompt_row(
    out_port: object,
    serial_collector: object,
    *,
    target_row: int,
    channel_1based: int,
    press_ms: int,
    phase_wait_ms: int,
    step_dwell_ms: int,
    send_short_press: object,
    log_prefix: str,
    choice: str,
) -> int:
    anchor = len(serial_collector.snapshot())
    midi_scroll_overlay(
        out_port,
        steps=target_row,
        direction="down",
        channel_1based=channel_1based,
        press_ms=press_ms,
        phase_wait_ms=phase_wait_ms,
        send_short_press=send_short_press,
        log_prefix=log_prefix,
        row_label=f"dirty prompt {choice} (row {target_row})",
    )
    if wait_serial_cap_ready(serial_collector, timeout_ms=500):
        wait_overlay_selection(
            serial_collector,
            mode=1,
            row=target_row,
            after_line_index=anchor,
            timeout_s=5.0,
            log_prefix=log_prefix,
        )
    dwell_ms(step_dwell_ms, log_prefix, f"dirty prompt {choice}")
    return anchor

def make_workspace_dirty_again(
    out_port,
    in_port,
    serial_collector,
    *,
    track_number: int,
    record_bars: int,
    press_ms: int,
    phase_wait_ms: int,
    save_drain_s: float,
    log_prefix: str,
) -> bool:
    from hitl.deferred_save_idle import wait_for_deferred_save_idle
    from hitl.transport_clock import wait_for_phase_clocks
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        RECORD_BUTTON_NOTE,
        MIDI_CLOCKS_PER_BAR,
    )
    from hitl.midi_io import _send_short_press
    from hitl.edit_controls import (
        _ensure_transport_running,
        _stop_transport_if_running,
    )

    transport_args = argparse.Namespace(
        serial_port="1" if serial_collector is not None else None,
        follow_current_session=False,
        follow_serial_log=None,
        tempo_bpm=120.0,
    )

    print(f"{log_prefix} make workspace dirty: {record_bars}-bar record on track {track_number}")
    _ensure_transport_running(
        out_port,
        in_port,
        press_ms=press_ms,
        phase_wait_ms=phase_wait_ms,
        serial_collector=serial_collector,
        transport_args=transport_args,
    )
    time.sleep(phase_wait_ms / 1000.0)

    dirty_record_anchor = len(serial_collector.snapshot())
    _send_short_press(
        out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    time.sleep(phase_wait_ms / 1000.0)

    target_clocks = record_bars * MIDI_CLOCKS_PER_BAR
    seen = wait_for_phase_clocks(
        in_port,
        serial_collector,
        transport_args,
        target_clocks,
        timeout_s=max(30.0, record_bars * 6.0),
    )
    if seen < target_clocks:
        print(f"{log_prefix} warn: record clock wait incomplete ({seen}/{target_clocks})")
    time.sleep(phase_wait_ms / 1000.0)

    _send_short_press(
        out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    time.sleep(phase_wait_ms / 1000.0)

    if not wait_for_deferred_save_idle(
        serial_collector,
        after_line_index=dirty_record_anchor,
        timeout_s=save_drain_s,
        log_prefix=log_prefix,
    ):
        print(f"{log_prefix} error: deferred save did not finish after dirty record")
        return False

    stop_anchor = len(serial_collector.snapshot())
    print(f"{log_prefix} transport stop before load request")
    _stop_transport_if_running(
        out_port,
        in_port,
        press_ms=press_ms,
        phase_wait_ms=phase_wait_ms,
    )
    if not wait_for_deferred_save_idle(
        serial_collector,
        after_line_index=stop_anchor,
        timeout_s=save_drain_s,
        log_prefix=log_prefix,
    ):
        print(f"{log_prefix} warn: deferred save still active after transport stop")
    print(f"{log_prefix} dirty record saved (workspace should be dirty)")
    return True
