"""Transport-stop idle save skips clean slots; record-stop writes one dirty slot."""

from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

from hitl.context import get_context
from hitl.verify.current_set_incremental_save import (
    persistence_result_ok_after_line,
    verify_current_set_incremental_save,
)


def _parse_common_args(args: object) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--track-number", "--track", type=int, default=5)
    parser.add_argument("--midi-channel", type=int, default=None)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--play-wait-ms", type=int, default=1500)
    parser.add_argument("--deferred-save-wait-ms", type=int, default=3000)
    parser.add_argument("--record-bars", type=int, default=2)
    parser.add_argument("--state-sync-timeout-ms", type=int, default=8000)
    parser.add_argument("--clear-press-ms", type=int, default=900)
    parser.add_argument(
        "--no-record-incremental-save",
        action="store_false",
        dest="verify_record_incremental_save",
        default=True,
        help="Skip record-stop phase (transport-stop w0_s64 only)",
    )
    legacy = list(getattr(args, "legacy_args", []) or [])
    return parser.parse_args(legacy)


def _wait_for_persistence_after_line(
    collector,
    *,
    after_line_index: int,
    timeout_s: float,
) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        lines = collector.snapshot()
        if persistence_result_ok_after_line(lines, after_line_index):
            return True
        time.sleep(0.05)
    return False


def run_current_set_incremental_save(args: object) -> int:
    import mido
    from host_midi_automation_baseline import (
        CONTROL_CHANNEL_1BASED,
        RECORD_BUTTON_NOTE,
        RunAbort,
        SerialCaptureCollector,
        TRACK_SELECT_NOTE_BASE,
        _drain_input_messages,
        _find_midi_port,
        _send_short_press,
        _wait_for_transition_count,
    )
    from host_midi_automation_edit_baseline import (
        EDIT_RECORD_FIXTURE,
        _ensure_clear_to_empty,
        _ensure_recording_started,
        _ensure_transport_running,
        _stop_transport_if_running,
        _stream_fixture_record,
    )

    ns = _parse_common_args(args)
    setattr(args, "verify_record_incremental_save", ns.verify_record_incremental_save)
    setattr(args, "record_stop_serial_anchor", -1)
    ctx = get_context(args)
    exit_code = 0
    log_prefix = "[current-set-incremental-save-hitl]"
    midi_channel = ns.midi_channel if ns.midi_channel is not None else ns.track_number
    save_wait_s = ns.deferred_save_wait_ms / 1000.0
    abort = RunAbort()

    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    serial_collector: Optional[SerialCaptureCollector] = None

    try:
        if not ns.serial_port:
            print(f"{log_prefix} error: --serial-port is required")
            return 2

        serial_collector = SerialCaptureCollector(ns.serial_port, baud=ns.serial_baud)
        serial_collector.start()
        time.sleep(0.3)

        print(f"{log_prefix} select track {ns.track_number}")
        _send_short_press(
            out_port,
            note=TRACK_SELECT_NOTE_BASE + ns.track_number,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        time.sleep(ns.phase_wait_ms / 1000.0)

        print(f"{log_prefix} start transport")
        _ensure_transport_running(
            out_port,
            in_port,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
        )
        print(f"{log_prefix} short play ({ns.play_wait_ms}ms)")
        time.sleep(ns.play_wait_ms / 1000.0)

        print(f"{log_prefix} transport stop (idle save)")
        transport_stop_anchor = len(serial_collector.snapshot())
        setattr(args, "transport_stop_serial_anchor", transport_stop_anchor)
        _stop_transport_if_running(
            out_port,
            in_port,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
        )
        ctx.markers.append("phase:transport_stop_idle")
        if not _wait_for_persistence_after_line(
            serial_collector,
            after_line_index=transport_stop_anchor,
            timeout_s=save_wait_s,
        ):
            print(f"{log_prefix} warn: timed out waiting for PERS,result,ok after transport stop")
        else:
            print(f"{log_prefix} deferred save completed after transport stop")
        time.sleep(ns.phase_wait_ms / 1000.0)

        if ns.verify_record_incremental_save:
            print(f"{log_prefix} clear selected loop before record")
            _ensure_clear_to_empty(
                out_port,
                serial_collector,
                clear_press_ms=ns.clear_press_ms,
                state_sync_timeout_ms=ns.state_sync_timeout_ms,
                abort=abort,
            )
            time.sleep(ns.phase_wait_ms / 1000.0)

            print(f"{log_prefix} start transport for record")
            _ensure_transport_running(
                out_port,
                in_port,
                press_ms=ns.press_ms,
                phase_wait_ms=ns.phase_wait_ms,
            )
            print(f"{log_prefix} record {ns.record_bars} bars")
            if not _ensure_recording_started(
                out_port,
                serial_collector,
                press_ms=ns.press_ms,
                state_sync_timeout_ms=ns.state_sync_timeout_ms,
                abort=abort,
            ):
                print(f"{log_prefix} warn: record start not confirmed")
            _stream_fixture_record(
                out_port,
                in_port,
                fixture=EDIT_RECORD_FIXTURE,
                target_bars=ns.record_bars,
                midi_channel_1based=midi_channel,
                press_ms=ns.press_ms,
                abort=abort,
            )
            print(f"{log_prefix} record stop")
            record_stop_anchor = len(serial_collector.snapshot())
            setattr(args, "record_stop_serial_anchor", record_stop_anchor)
            _send_short_press(
                out_port,
                note=RECORD_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
            )
            ctx.markers.append("phase:record_stop")
            reached_playing = _wait_for_transition_count(
                serial_collector,
                from_state="RECORDING",
                to_state="STOPPED_RECORDING",
                target_count=1,
                timeout_s=ns.state_sync_timeout_ms / 1000.0,
                abort=abort,
            )
            if not reached_playing:
                print(f"{log_prefix} warn: RECORDING->STOPPED_RECORDING not confirmed")
            if not _wait_for_persistence_after_line(
                serial_collector,
                after_line_index=record_stop_anchor,
                timeout_s=save_wait_s,
            ):
                print(f"{log_prefix} warn: timed out waiting for PERS,result,ok after record stop")
            else:
                print(f"{log_prefix} deferred save completed after record stop")
            time.sleep(ns.phase_wait_ms / 1000.0)

        lines = serial_collector.snapshot()
        check = verify_current_set_incremental_save(lines, args)
        print(f"{log_prefix} serial verification ok={check.get('ok')}")
        for issue in check.get("issues", []):
            print(f"  issue: {issue}")

        out_dir = Path(getattr(args, "out_dir", Path("captures")))
        out_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        report_path = out_dir / f"host_midi_hitl_current_set_incremental_save_{stamp}.json"
        report_path.write_text(
            json.dumps(
                {
                    "scenario": "current_set_incremental_save",
                    "track_number": ns.track_number,
                    "transport_stop_stats": check.get("transport_stop_stats"),
                    "record_stop_stats": check.get("record_stop_stats"),
                    "serial_verification": check,
                    "markers": ctx.markers,
                },
                indent=2,
            )
        )
        print(f"{log_prefix} report: {report_path}")
        if not check.get("ok", False):
            exit_code = 2
        return exit_code
    finally:
        if serial_collector is not None:
            serial_collector.stop()
        out_port.close()
        in_port.close()
        _drain_input_messages(in_port)
