"""Overlay list scroll + dirty-prompt scroll/cancel via serial (!OVERLAY_* hooks).

Substitutes for GPIO encoder rotation and encoder-button confirm when hardware
encoder is not wired yet.
"""

from __future__ import annotations

import argparse
import json
import re
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

from hitl.context import get_context
from hitl.deferred_save_idle import wait_for_deferred_save_idle
from hitl.scenarios.revision_load_dirty import _make_workspace_dirty_again
from hitl.scenarios.revision_load import _wait_for_pattern_after
from hitl.verify.load_save_display import last_load_save_mode_active
from hitl.verify.load_save_overlay_scroll import verify_load_save_overlay_scroll

_LDSV_RE = re.compile(r"#CAP,\d+,LDSV,([01])")
_REV_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_complete,\d+,\d+,\d+,S(\d{4})_v(\d{4})\b"
)
_REV_LOAD_DIRTY_PROMPT_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_dirty_prompt,\d+,\d+,\d+,shown\b"
)
_OVLY_SEL_RE = re.compile(r"#CAP,\d+,OVLY,sel,(\d+),(\d+)\b")


def _wait_load_save_mode(serial_collector: object, expected: int, timeout_ms: int) -> bool:
    deadline = time.time() + timeout_ms / 1000.0
    while time.time() < deadline:
        if last_load_save_mode_active(serial_collector.snapshot()) == expected:
            return True
        time.sleep(0.05)
    return last_load_save_mode_active(serial_collector.snapshot()) == expected


def _wait_serial_cap_ready(serial_collector: object, timeout_ms: int = 8000) -> bool:
    deadline = time.time() + timeout_ms / 1000.0
    while time.time() < deadline:
        for line in serial_collector.snapshot():
            if "#CAP," in line:
                return True
        time.sleep(0.05)
    return False


def _wait_overlay_selection(
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
    print(f"{log_prefix} error: timed out waiting for OVLY,sel,{mode},{row}")
    return False


def _dwell_on_overlay_row(ns: argparse.Namespace, log_prefix: str, row_label: str) -> None:
    dwell_ms = int(getattr(ns, "overlay_scroll_step_dwell_ms", 0) or 0)
    if dwell_ms <= 0:
        return
    print(f"{log_prefix} dwell {dwell_ms}ms on {row_label}")
    time.sleep(dwell_ms / 1000.0)


def _parse_overlay_scroll_args(args: object) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--track-number", "--track", type=int, default=5)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--deferred-save-wait-ms", type=int, default=120000)
    parser.add_argument("--revision-commit-wait-ms", type=int, default=180000)
    parser.add_argument(
        "--overlay-scroll-only",
        action="store_true",
        default=False,
        help="Root list scroll only; skip record/dirty-prompt phase",
    )
    parser.add_argument(
        "--overlay-scroll-step-dwell-ms",
        type=int,
        default=3000,
        help="Pause on each root/dirty list row after scroll (0 = immediate)",
    )
    parser.add_argument(
        "--overlay-root-dwell-ms",
        type=int,
        default=None,
        help="Pause with root overlay open before exit (default 5000 full / 12000 scroll-only)",
    )
    parser.add_argument(
        "--overlay-dirty-dwell-ms",
        type=int,
        default=3000,
        help="Pause on dirty Cancel row before confirm (0 = immediate)",
    )
    legacy = list(getattr(args, "legacy_args", []) or [])
    ns = parser.parse_args(legacy)
    if ns.overlay_root_dwell_ms is None:
        ns.overlay_root_dwell_ms = 12000 if ns.overlay_scroll_only else 5000
    return ns


def run_load_save_overlay_scroll(args: object) -> int:
    from host_midi_automation_baseline import (
        CONTROL_CHANNEL_1BASED,
        SerialCaptureCollector,
        _drain_input_messages,
        _find_midi_port,
        _send_short_press,
    )

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
        time.sleep(0.5)
        if not _wait_serial_cap_ready(serial_collector):
            print(f"{log_prefix} warn: serial CAP not ready")

        verify_offset = len(serial_collector.snapshot())

        # --- Phase A: root list scroll (Save -> Current -> Save) ---
        print(f"{log_prefix} serial !OVERLAY_ENTER")
        serial_collector.write_line("!OVERLAY_ENTER")
        time.sleep(0.3)
        if not _wait_load_save_mode(serial_collector, 1, 1500):
            print(f"{log_prefix} error: overlay did not enter")
            return 2
        ctx.markers.append("phase:overlay_enter")
        _dwell_on_overlay_row(ns, log_prefix, "Save row")

        root_scroll_anchor = len(serial_collector.snapshot())
        setattr(args, "overlay_root_scroll_anchor", root_scroll_anchor - verify_offset)

        print(f"{log_prefix} serial !OVERLAY_SCROLL 1 (Save -> Current)")
        serial_collector.write_line("!OVERLAY_SCROLL 1")
        if not _wait_overlay_selection(
            serial_collector,
            mode=0,
            row=1,
            after_line_index=root_scroll_anchor,
            timeout_s=2.0,
            log_prefix=log_prefix,
        ):
            return 2
        ctx.markers.append("phase:root_scroll_current")
        _dwell_on_overlay_row(ns, log_prefix, "Current row")

        print(f"{log_prefix} serial !OVERLAY_SCROLL 1 (Current -> catalog set)")
        serial_collector.write_line("!OVERLAY_SCROLL 1")
        if _wait_overlay_selection(
            serial_collector,
            mode=0,
            row=2,
            after_line_index=root_scroll_anchor,
            timeout_s=2.0,
            log_prefix=log_prefix,
        ):
            ctx.markers.append("phase:root_scroll_catalog_set")
            _dwell_on_overlay_row(ns, log_prefix, "catalog set row")
        else:
            print(f"{log_prefix} note: no catalog set row (OVLY,sel,0,2); continuing")

        print(f"{log_prefix} serial !OVERLAY_SCROLL -1 (catalog set -> Current)")
        serial_collector.write_line("!OVERLAY_SCROLL -1")
        _wait_overlay_selection(
            serial_collector,
            mode=0,
            row=1,
            after_line_index=root_scroll_anchor,
            timeout_s=2.0,
            log_prefix=log_prefix,
        )
        _dwell_on_overlay_row(ns, log_prefix, "Current row")

        print(f"{log_prefix} serial !OVERLAY_SCROLL -1 (Current -> Save)")
        serial_collector.write_line("!OVERLAY_SCROLL -1")
        if not _wait_overlay_selection(
            serial_collector,
            mode=0,
            row=0,
            after_line_index=root_scroll_anchor,
            timeout_s=2.0,
            log_prefix=log_prefix,
        ):
            return 2
        ctx.markers.append("phase:root_scroll_save")
        _dwell_on_overlay_row(ns, log_prefix, "Save row")

        root_dwell_ms = int(ns.overlay_root_dwell_ms)
        if root_dwell_ms > 0:
            print(f"{log_prefix} root overlay dwell {root_dwell_ms}ms (visible before Phase A exit)")
            time.sleep(root_dwell_ms / 1000.0)
        ctx.markers.append("phase:overlay_root_dwell_end")

        print(f"{log_prefix} serial !OVERLAY_EXIT (end phase A)")
        serial_collector.write_line("!OVERLAY_EXIT")
        time.sleep(0.3)
        _wait_load_save_mode(serial_collector, 0, 1500)
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
                        "track_number": ns.track_number,
                        "overlay_scroll_step_dwell_ms": ns.overlay_scroll_step_dwell_ms,
                        "overlay_root_dwell_ms": ns.overlay_root_dwell_ms,
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

        # --- Phase B: dirty prompt scroll to Cancel + confirm ---
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
            print(f"{log_prefix} error: timed out waiting for setup rev_complete")
            return 2
        set_id = int(setup_match.group(1))
        revision_id = int(setup_match.group(2))
        ctx.markers.append("phase:setup_revision_commit")

        dirty_record_anchor = len(serial_collector.snapshot())
        if not _make_workspace_dirty_again(
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

        print(f"{log_prefix} serial !OVERLAY_ENTER (dirty prompt host)")
        serial_collector.write_line("!OVERLAY_ENTER")
        time.sleep(0.3)
        if not _wait_load_save_mode(serial_collector, 1, 1500):
            print(f"{log_prefix} error: overlay did not enter for dirty prompt")
            return 2

        load_anchor = len(serial_collector.snapshot())
        setattr(args, "overlay_dirty_load_anchor", load_anchor - verify_offset)
        load_command = f"!REV_LOAD {set_id} {revision_id}"
        print(f"{log_prefix} serial {load_command}")
        serial_collector.write_line(load_command)
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
            print(f"{log_prefix} error: timed out waiting for rev_load_dirty_prompt")
            return 2
        ctx.markers.append("phase:dirty_prompt_shown")

        dirty_scroll_anchor = len(serial_collector.snapshot())
        setattr(args, "overlay_dirty_scroll_anchor", dirty_scroll_anchor - verify_offset)

        print(f"{log_prefix} serial !OVERLAY_SCROLL 2 (Yes -> Cancel)")
        serial_collector.write_line("!OVERLAY_SCROLL 2")
        if not _wait_overlay_selection(
            serial_collector,
            mode=1,
            row=2,
            after_line_index=dirty_scroll_anchor,
            timeout_s=2.0,
            log_prefix=log_prefix,
        ):
            return 2
        ctx.markers.append("phase:dirty_scroll_cancel")
        _dwell_on_overlay_row(ns, log_prefix, "dirty prompt Cancel row")

        dirty_dwell_ms = int(ns.overlay_dirty_dwell_ms)
        if dirty_dwell_ms > 0:
            print(
                f"{log_prefix} dirty prompt dwell {dirty_dwell_ms}ms "
                "(Cancel row highlighted before confirm)"
            )
            time.sleep(dirty_dwell_ms / 1000.0)
        ctx.markers.append("phase:overlay_dirty_dwell_end")

        dirty_confirm_anchor = len(serial_collector.snapshot())
        setattr(args, "overlay_dirty_confirm_anchor", dirty_confirm_anchor - verify_offset)

        print(f"{log_prefix} serial !OVERLAY_CONFIRM (Cancel)")
        serial_collector.write_line("!OVERLAY_CONFIRM")
        time.sleep(1.0)
        ctx.markers.append("phase:dirty_confirm_cancel")

        print(f"{log_prefix} serial !OVERLAY_EXIT")
        serial_collector.write_line("!OVERLAY_EXIT")
        time.sleep(0.3)
        _wait_load_save_mode(serial_collector, 0, 1500)
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
                    "track_number": ns.track_number,
                    "setup_set_id": set_id,
                    "setup_revision_id": revision_id,
                    "overlay_scroll_step_dwell_ms": ns.overlay_scroll_step_dwell_ms,
                    "overlay_root_dwell_ms": ns.overlay_root_dwell_ms,
                    "overlay_dirty_dwell_ms": ns.overlay_dirty_dwell_ms,
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


def run_load_save_overlay_scroll_only(args: object) -> int:
    legacy = list(getattr(args, "legacy_args", []) or [])
    if "--overlay-scroll-only" not in legacy:
        legacy.append("--overlay-scroll-only")
    setattr(args, "legacy_args", legacy)
    return run_load_save_overlay_scroll(args)
