"""Play/Stop double-press toggles load/save set browser overlay."""

from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

from hitl.context import get_context
from hitl.verify.load_save_display import (
    last_load_save_mode_active,
    verify_load_save_display,
)


def _parse_common_args(args: object) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--track-number", type=int, default=5)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--double-press-gap-ms", type=int, default=80)
    parser.add_argument("--gesture-settle-ms", type=int, default=600)
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--load-save-display-wait-ms", type=int, default=10000)
    parser.add_argument("--start-transport", action="store_true", default=False)
    legacy = list(getattr(args, "legacy_args", []) or [])
    return parser.parse_args(legacy)


def _track_select_note(track_number_1based: int) -> int:
    from host_midi_automation_baseline import TRACK_SELECT_NOTE_BASE

    if not (1 <= track_number_1based <= 8):
        raise ValueError(f"track-number must be 1-8, got {track_number_1based}")
    return TRACK_SELECT_NOTE_BASE + (track_number_1based - 1)


def _sync_load_save_overlay_closed(
    out_port: object,
    serial_collector: Optional[object],
    *,
    note: int,
    channel_1based: int,
    press_ms: int,
    gap_ms: int,
    gesture_settle_ms: int,
    send_double_press: object,
) -> int:
    """Idempotently close overlay; return serial line index after sync for verification."""
    if serial_collector is None:
        return 0
    for attempt in range(2):
        time.sleep(0.2)
        if last_load_save_mode_active(serial_collector.snapshot()) == 0:
            return len(serial_collector.snapshot())
        print(
            f"[load-save-display-hitl] sync overlay closed (attempt {attempt + 1}/2)"
        )
        send_double_press(
            out_port,
            note=note,
            channel_1based=channel_1based,
            press_ms=press_ms,
            gap_ms=gap_ms,
        )
        time.sleep(gesture_settle_ms / 1000.0)
    return len(serial_collector.snapshot())


def run_load_save_display(args: object) -> int:
    import mido
    from host_midi_automation_baseline import (
        CONTROL_CHANNEL_1BASED,
        PLAY_STOP_BUTTON_NOTE,
        SerialCaptureCollector,
        _drain_input_messages,
        _find_midi_port,
        _send_short_press,
    )
    from host_midi_automation_edit_baseline import (
        _ensure_transport_running,
        _send_double_press,
    )

    ns = _parse_common_args(args)
    ctx = get_context(args)
    exit_code = 0

    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    serial_collector: Optional[SerialCaptureCollector] = None
    serial_verify_offset = 0

    try:
        if ns.serial_port:
            serial_collector = SerialCaptureCollector(ns.serial_port, baud=ns.serial_baud)
            serial_collector.start()

        serial_verify_offset = _sync_load_save_overlay_closed(
            out_port,
            serial_collector,
            note=PLAY_STOP_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
            gap_ms=ns.double_press_gap_ms,
            gesture_settle_ms=ns.gesture_settle_ms,
            send_double_press=_send_double_press,
        )

        if ns.start_transport:
            _ensure_transport_running(
                out_port,
                in_port,
                press_ms=ns.press_ms,
                phase_wait_ms=ns.phase_wait_ms,
            )

        print(f"[load-save-display-hitl] select track {ns.track_number}")
        _send_short_press(
            out_port,
            note=_track_select_note(ns.track_number),
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        time.sleep(ns.phase_wait_ms / 1000.0)

        print("[load-save-display-hitl] play/stop double press — enter load/save")
        _send_double_press(
            out_port,
            note=PLAY_STOP_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
            gap_ms=ns.double_press_gap_ms,
        )
        print(
            f"[load-save-display-hitl] gesture settle {ns.gesture_settle_ms}ms "
            "(double-press dispatch)"
        )
        time.sleep(ns.gesture_settle_ms / 1000.0)
        ctx.markers.append("phase:load_save_enter")
        print(
            f"[load-save-display-hitl] waiting {ns.load_save_display_wait_ms}ms before exit"
        )
        time.sleep(ns.load_save_display_wait_ms / 1000.0)
        ctx.markers.append("phase:load_save_display_wait_end")

        print("[load-save-display-hitl] play/stop double press — exit load/save")
        _send_double_press(
            out_port,
            note=PLAY_STOP_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
            gap_ms=ns.double_press_gap_ms,
        )
        time.sleep(ns.gesture_settle_ms / 1000.0)
        ctx.markers.append("phase:load_save_exit")
        time.sleep(ns.phase_wait_ms / 1000.0)

        if serial_collector is not None:
            lines = serial_collector.snapshot()[serial_verify_offset:]
            check = verify_load_save_display(lines, args)
            print(f"[load-save-display-hitl] serial verification ok={check.get('ok')}")
            for issue in check.get("issues", []):
                print(f"  issue: {issue}")

            out_dir = Path(getattr(args, "out_dir", Path("captures")))
            out_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            report_path = out_dir / f"host_midi_hitl_load_save_display_{stamp}.json"
            report_path.write_text(
                json.dumps(
                    {
                        "scenario": "load_save_display",
                        "track_number": ns.track_number,
                        "press_ms": ns.press_ms,
                        "double_press_gap_ms": ns.double_press_gap_ms,
                        "gesture_settle_ms": ns.gesture_settle_ms,
                        "load_save_display_wait_ms": ns.load_save_display_wait_ms,
                        "serial_verification": check,
                        "markers": ctx.markers,
                    },
                    indent=2,
                )
            )
            print(f"[load-save-display-hitl] report: {report_path}")
            if not check.get("ok", False):
                exit_code = 2
        else:
            print("[load-save-display-hitl] warn: no --serial-port; skipping verification")
            exit_code = 1

        return exit_code
    finally:
        if serial_collector is not None:
            serial_collector.stop()
        out_port.close()
        in_port.close()
        _drain_input_messages(in_port)
