"""Overlay list scroll + dirty-prompt via MIDI (watchable on hardware OLED).

Enter/exit: edit or play/stop double-press.
Scroll down: record short. Scroll up: track short.
Confirm: edit short.
"""

from __future__ import annotations

import json
import re
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

from hitl.context import get_context
from hitl.scenarios.load_save_overlay_helpers import (
    dwell_ms,
    enter_load_save_overlay,
    exit_load_save_overlay,
    make_workspace_dirty_again,
    midi_confirm_overlay_row,
    midi_scroll_dirty_prompt_row,
    midi_scroll_overlay,
    midi_scroll_to_root_row,
    overlay_scroll_steps_to_set_row,
    parse_overlay_watch_args,
    wait_load_save_mode,
    wait_overlay_selection,
    wait_serial_cap_ready,
)
from hitl.scenarios.revision_load import _parse_common_args, _wait_for_pattern_after
from hitl.verify.load_save_overlay_scroll import verify_load_save_overlay_scroll

_REV_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_complete,\d+,\d+,\d+,S(\d{4})_v(\d{4})\b"
)
_REV_LOAD_DIRTY_PROMPT_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_dirty_prompt,\d+,\d+,\d+,shown\b"
)

def _parse_overlay_scroll_args(args: object):
    watch_ns = parse_overlay_watch_args(args)
    parser = __import__("argparse").ArgumentParser(add_help=False)
    parser.add_argument("--deferred-save-wait-ms", type=int, default=120000)
    parser.add_argument("--revision-commit-wait-ms", type=int, default=180000)
    parser.add_argument(
        "--overlay-scroll-only",
        action="store_true",
        default=False,
    )
    legacy = list(getattr(args, "legacy_args", []) or [])
    extra, _unknown = parser.parse_known_args(legacy)
    watch_ns.deferred_save_wait_ms = extra.deferred_save_wait_ms
    watch_ns.revision_commit_wait_ms = extra.revision_commit_wait_ms
    watch_ns.overlay_scroll_only = extra.overlay_scroll_only
    if not hasattr(watch_ns, "overlay_root_dwell_ms") or watch_ns.overlay_root_dwell_ms is None:
        watch_ns.overlay_root_dwell_ms = 12000 if extra.overlay_scroll_only else 5000
    return watch_ns

# Re-export for load_save_overlay_load.py
_wait_load_save_mode = wait_load_save_mode
_wait_overlay_selection = wait_overlay_selection
_wait_serial_cap_ready = wait_serial_cap_ready

def run_load_save_overlay_scroll(args: object) -> int:
    from hitl.control_constants import CONTROL_CHANNEL_1BASED
    from hitl.serial_collector import SerialCaptureCollector
    from hitl.midi_io import (
        _drain_input_messages,
        _find_midi_port,
        _send_short_press,
    )
    from hitl.edit_controls import _send_double_press

    ns = _parse_overlay_scroll_args(args)
    ctx = get_context(args)
    log_prefix = "[load-save-overlay-scroll-hitl]"
    save_drain_s = ns.deferred_save_wait_ms / 1000.0
    commit_wait_s = ns.revision_commit_wait_ms / 1000.0
    record_bars = int(getattr(args, "record_bars", None) or 2)

    import mido

    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    serial_collector: Optional[SerialCaptureCollector] = None
    exit_code = 0

    try:
        if not ns.serial_port:
            print(f"{log_prefix} error: --serial-port is required")
            return 2

        serial_collector = SerialCaptureCollector(ns.serial_port, baud=ns.serial_baud)
        serial_collector.start()
        time.sleep(1.0)
        if not wait_serial_cap_ready(serial_collector):
            print(f"{log_prefix} warn: serial CAP not ready")

        verify_offset = len(serial_collector.snapshot())

        # --- Phase A: root list scroll (Save -> Current -> Set -> Current -> Save) ---
        if not enter_load_save_overlay(
            out_port,
            serial_collector,
            press_ms=ns.press_ms,
            gap_ms=ns.double_press_gap_ms,
            gesture_settle_ms=ns.gesture_settle_ms,
            send_double_press=_send_double_press,
            log_prefix=log_prefix,
        ):
            return 2
        ctx.markers.append("phase:overlay_enter_midi")
        dwell_ms(ns.overlay_scroll_step_dwell_ms, log_prefix, "Save row")

        root_scroll_anchor = len(serial_collector.snapshot())
        setattr(args, "overlay_root_scroll_anchor", root_scroll_anchor - verify_offset)

        midi_scroll_overlay(
            out_port,
            steps=1,
            direction="down",
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
            row_label="Current row",
        )
        _wait_overlay_selection(
            serial_collector, mode=0, row=1,
            after_line_index=root_scroll_anchor, timeout_s=3.0, log_prefix=log_prefix,
        )
        dwell_ms(ns.overlay_scroll_step_dwell_ms, log_prefix, "Current row")
        ctx.markers.append("phase:root_scroll_current")

        midi_scroll_overlay(
            out_port,
            steps=1,
            direction="down",
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
            row_label="catalog set row",
        )
        if _wait_overlay_selection(
            serial_collector, mode=0, row=2,
            after_line_index=root_scroll_anchor, timeout_s=3.0, log_prefix=log_prefix,
        ):
            ctx.markers.append("phase:root_scroll_catalog_set")
            dwell_ms(ns.overlay_scroll_step_dwell_ms, log_prefix, "catalog set row")
        else:
            print(f"{log_prefix} note: no catalog set row; continuing")

        midi_scroll_overlay(
            out_port,
            steps=1,
            direction="up",
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
            row_label="Current row",
        )
        dwell_ms(ns.overlay_scroll_step_dwell_ms, log_prefix, "Current row")

        midi_scroll_overlay(
            out_port,
            steps=1,
            direction="up",
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
            row_label="Save row",
        )
        _wait_overlay_selection(
            serial_collector, mode=0, row=0,
            after_line_index=root_scroll_anchor, timeout_s=3.0, log_prefix=log_prefix,
        )
        ctx.markers.append("phase:root_scroll_save")
        dwell_ms(ns.overlay_scroll_step_dwell_ms, log_prefix, "Save row")

        dwell_ms(int(ns.overlay_root_dwell_ms), log_prefix, "root overlay before exit")
        ctx.markers.append("phase:overlay_root_dwell_end")

        exit_load_save_overlay(
            out_port,
            serial_collector,
            press_ms=ns.press_ms,
            gap_ms=ns.double_press_gap_ms,
            gesture_settle_ms=ns.gesture_settle_ms,
            send_double_press=_send_double_press,
            log_prefix=log_prefix,
        )
        ctx.markers.append("phase:overlay_exit_root")

        setattr(args, "overlay_scroll_only", bool(ns.overlay_scroll_only))
        if ns.overlay_scroll_only:
            lines = serial_collector.snapshot()[verify_offset:]
            check = verify_load_save_overlay_scroll(lines, args)
            print(f"{log_prefix} serial verification ok={check.get('ok')}")
            for issue in check.get("issues", []):
                print(f"  issue: {issue}")
            out_dir = Path(getattr(args, "out_dir", Path("captures")))
            out_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            report_path = out_dir / f"host_midi_hitl_load_save_overlay_scroll_only_{stamp}.json"
            report_path.write_text(
                json.dumps(
                    {
                        "scenario": "load_save_overlay_scroll_only",
                        "overlay_scroll_step_dwell_ms": ns.overlay_scroll_step_dwell_ms,
                        "serial_verification": check,
                        "markers": ctx.markers,
                    },
                    indent=2,
                )
            )
            print(f"{log_prefix} report: {report_path}")
            return 0 if check.get("ok", False) else 2

        # --- Phase B: dirty prompt scroll to Cancel + confirm (overlay MIDI) ---
        setup_commit_anchor = len(serial_collector.snapshot())
        print(f"{log_prefix} serial !REV_COMMIT (setup revision)")
        serial_collector.write_line("!REV_COMMIT")
        setup_match = _wait_for_pattern_after(
            serial_collector,
            _REV_COMPLETE_RE,
            after_line_index=setup_commit_anchor,
            timeout_s=commit_wait_s,
            log_prefix=log_prefix,
            label="setup rev_complete",
        )
        if setup_match is None:
            return 2
        set_id = int(setup_match.group(1))
        revision_id = int(setup_match.group(2))
        ctx.markers.append("phase:setup_revision_commit")

        if not make_workspace_dirty_again(
            out_port,
            in_port,
            serial_collector,
            track_number=ns.track_number,
            record_bars=record_bars,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
            save_drain_s=save_drain_s,
            log_prefix=log_prefix,
        ):
            return 2
        ctx.markers.append("phase:dirty_record")

        target_row = overlay_scroll_steps_to_set_row(0)
        if not enter_load_save_overlay(
            out_port,
            serial_collector,
            press_ms=ns.press_ms,
            gap_ms=ns.double_press_gap_ms,
            gesture_settle_ms=ns.gesture_settle_ms,
            send_double_press=_send_double_press,
            log_prefix=log_prefix,
        ):
            return 2

        midi_scroll_to_root_row(
            out_port,
            serial_collector,
            target_row=target_row,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
            step_dwell_ms=ns.overlay_scroll_step_dwell_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
        )

        load_anchor = len(serial_collector.snapshot())
        setattr(args, "overlay_dirty_load_anchor", load_anchor - verify_offset)
        midi_confirm_overlay_row(
            out_port,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
            gesture_settle_ms=ns.gesture_settle_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
            action_label="load Set (expect dirty prompt)",
        )
        if (
            _wait_for_pattern_after(
                serial_collector,
                _REV_LOAD_DIRTY_PROMPT_RE,
                after_line_index=load_anchor,
                timeout_s=30.0,
                log_prefix=log_prefix,
                label="rev_load_dirty_prompt",
            )
            is None
        ):
            return 2
        ctx.markers.append("phase:dirty_prompt_shown")

        dirty_scroll_anchor = len(serial_collector.snapshot())
        setattr(args, "overlay_dirty_scroll_anchor", dirty_scroll_anchor - verify_offset)
        midi_scroll_dirty_prompt_row(
            out_port,
            serial_collector,
            target_row=2,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
            step_dwell_ms=ns.overlay_dirty_dwell_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
            choice="cancel",
        )
        ctx.markers.append("phase:dirty_scroll_cancel")

        dirty_confirm_anchor = len(serial_collector.snapshot())
        setattr(args, "overlay_dirty_confirm_anchor", dirty_confirm_anchor - verify_offset)
        midi_confirm_overlay_row(
            out_port,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
            gesture_settle_ms=ns.gesture_settle_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
            action_label="dirty prompt Cancel",
        )
        ctx.markers.append("phase:dirty_confirm_cancel")
        time.sleep(1.0)

        exit_load_save_overlay(
            out_port,
            serial_collector,
            press_ms=ns.press_ms,
            gap_ms=ns.double_press_gap_ms,
            gesture_settle_ms=ns.gesture_settle_ms,
            send_double_press=_send_double_press,
            log_prefix=log_prefix,
        )
        ctx.markers.append("phase:overlay_exit_dirty")

        lines = serial_collector.snapshot()[verify_offset:]
        check = verify_load_save_overlay_scroll(lines, args)
        print(f"{log_prefix} serial verification ok={check.get('ok')}")
        for issue in check.get("issues", []):
            print(f"  issue: {issue}")
        out_dir = Path(getattr(args, "out_dir", Path("captures")))
        out_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        report_path = out_dir / f"host_midi_hitl_load_save_overlay_scroll_{stamp}.json"
        report_path.write_text(
            json.dumps(
                {
                    "scenario": "load_save_overlay_scroll",
                    "setup_set_id": set_id,
                    "setup_revision_id": revision_id,
                    "serial_verification": check,
                    "markers": ctx.markers,
                },
                indent=2,
            )
        )
        print(f"{log_prefix} report: {report_path}")
        return 0 if check.get("ok", False) else 2
    finally:
        if serial_collector is not None:
            serial_collector.stop()
        out_port.close()
        in_port.close()
        _drain_input_messages(in_port)

def run_load_save_overlay_scroll_only(args: object) -> int:
    legacy = list(getattr(args, "legacy_args", []) or [])
    if "--overlay-scroll-only" not in legacy:
        legacy.append("--overlay-scroll-only")
    setattr(args, "legacy_args", legacy)
    return run_load_save_overlay_scroll(args)
