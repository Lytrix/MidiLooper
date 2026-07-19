"""Load a catalog Set through the load/save overlay using main-control MIDI (manual path).

Enter overlay: Edit mode (note 38) double-press.
Scroll list: Record (36) short = down, Track (37) short = up.
Confirm load: Edit mode (38) short on focused Set row.

Setup uses serial !REV_COMMIT only to ensure a revision exists on SD; the load itself
is driven entirely by MIDI overlay gestures (no !REV_LOAD / !OVERLAY_* hooks).
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
from hitl.deferred_save_idle import deferred_save_idle_in_tail, wait_for_deferred_save_idle
from hitl.scenarios.load_save_overlay_helpers import (
    DIRTY_PROMPT_ROW,
    dwell_ms,
    enter_load_save_overlay,
    midi_confirm_overlay_row,
    midi_scroll_dirty_prompt_row,
    midi_scroll_to_root_row,
    overlay_scroll_steps_to_set_row,
    parse_overlay_watch_args,
    recover_load_save_overlay,
    wait_overlay_selection,
    wait_serial_cap_ready,
)
from hitl.scenarios.load_save_overlay_scroll import _wait_load_save_mode
from hitl.scenarios.revision_load import _wait_for_pattern_after
from hitl.verify.load_save_overlay_load import verify_load_save_overlay_load

_REV_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_complete,\d+,\d+,\d+,S(\d{4})_v(\d{4})\b"
)
_REV_LOAD_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_complete,\d+,\d+,\d+,S\d{4}_v\d{4}\b"
)
_REV_LOAD_DIRTY_PROMPT_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_dirty_prompt,\d+,\d+,\d+,shown\b"
)
_REV_LOAD_DIRTY_YES_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_dirty_yes,\d+,\d+,\d+,save_then_load\b"
)

ROOT_FIRST_SET_ROW = 2
_DIRTY_PROMPT_ROW = DIRTY_PROMPT_ROW

def _parse_overlay_load_args(args: object) -> argparse.Namespace:
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
    parser.add_argument("--play-wait-ms", type=int, default=1500)
    parser.add_argument("--prior-save-drain-wait-ms", type=int, default=120000)
    parser.add_argument("--deferred-save-wait-ms", type=int, default=120000)
    parser.add_argument("--revision-commit-wait-ms", type=int, default=180000)
    parser.add_argument("--revision-load-wait-ms", type=int, default=180000)
    parser.add_argument("--expected-set-id", type=int, default=1)
    parser.add_argument(
        "--catalog-set-index",
        type=int,
        default=0,
        help="0 = first Set row in root list (after Save/Current)",
    )
    parser.add_argument(
        "--skip-setup-commit",
        action="store_true",
        default=False,
        help="Skip !REV_COMMIT; assume target Set revision already on SD",
    )
    parser.add_argument(
        "--skip-workspace-save-prelude",
        action="store_true",
        default=False,
        help="Skip transport-stop deferred-save drain before setup commit",
    )
    parser.add_argument(
        "--overlay-load-only",
        action="store_true",
        default=False,
        help="MIDI overlay load only: skip transport prelude and !REV_COMMIT setup",
    )
    parser.add_argument(
        "--dirty-prompt-choice",
        choices=("yes", "no", "cancel"),
        default="yes",
        help="When workspace is dirty: overlay confirm row (Yes=save+load, No=discard, Cancel)",
    )
    legacy = list(getattr(args, "legacy_args", []) or [])
    return parser.parse_args(legacy)

def _track_select_note(track_number_1based: int) -> int:
    from hitl.control_constants import TRACK_SELECT_NOTE_BASE

    return TRACK_SELECT_NOTE_BASE + (track_number_1based - 1)

def _overlay_scroll_steps_to_set_row(catalog_set_index: int) -> int:
    return ROOT_FIRST_SET_ROW + max(catalog_set_index, 0)

def _wait_serial_any_line(serial_collector: object, timeout_ms: int = 12000) -> bool:
    deadline = time.time() + timeout_ms / 1000.0
    while time.time() < deadline:
        if serial_collector.snapshot():
            return True
        if serial_collector.error():
            return False
        time.sleep(0.05)
    return bool(serial_collector.snapshot())

def run_load_save_overlay_load(args: object) -> int:
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_NOTE,
    )
    from hitl.serial_collector import SerialCaptureCollector
    from hitl.midi_io import (
        _drain_input_messages,
        _find_midi_port,
        _send_short_press,
    )
    from hitl.edit_controls import (
        _ensure_transport_running,
        _send_double_press,
        _stop_transport_if_running,
    )

    ns = _parse_overlay_load_args(args)
    watch = parse_overlay_watch_args(args)
    ns.overlay_scroll_step_dwell_ms = watch.overlay_scroll_step_dwell_ms
    ns.overlay_root_dwell_ms = watch.overlay_root_dwell_ms
    ns.overlay_dirty_dwell_ms = watch.overlay_dirty_dwell_ms
    if ns.overlay_load_only:
        ns.skip_workspace_save_prelude = True
        ns.skip_setup_commit = True
    ctx = get_context(args)
    log_prefix = "[load-save-overlay-load-hitl]"
    prior_drain_s = ns.prior_save_drain_wait_ms / 1000.0
    save_drain_s = ns.deferred_save_wait_ms / 1000.0
    commit_wait_s = ns.revision_commit_wait_ms / 1000.0
    load_wait_s = ns.revision_load_wait_ms / 1000.0
    target_root_row = overlay_scroll_steps_to_set_row(ns.catalog_set_index)

    setattr(args, "expected_set_id", ns.expected_set_id)
    setattr(args, "overlay_target_root_row", target_root_row)
    setattr(args, "dirty_prompt_choice", ns.dirty_prompt_choice)

    import mido

    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    serial_collector: Optional[SerialCaptureCollector] = None
    exit_code = 0
    set_id = ns.expected_set_id
    revision_id: Optional[int] = None

    try:
        if not ns.serial_port:
            print(f"{log_prefix} error: --serial-port is required")
            return 2

        serial_collector = SerialCaptureCollector(ns.serial_port, baud=ns.serial_baud)
        serial_collector.start()
        time.sleep(5.0)
        serial_error = serial_collector.error()
        if serial_error:
            print(
                f"{log_prefix} error: cannot read serial ({serial_error}). "
                "Close PlatformIO device monitor / other apps using this port."
            )
            return 2
        if not _wait_serial_any_line(serial_collector):
            print(f"{log_prefix} warn: no serial lines yet (is capture-serial firmware flashed?)")
        if not wait_serial_cap_ready(serial_collector):
            print(f"{log_prefix} warn: serial CAP not ready yet")
        print(
            f"{log_prefix} plan: skip_prelude={ns.skip_workspace_save_prelude} "
            f"skip_commit={ns.skip_setup_commit} target=S{ns.expected_set_id:04d} "
            f"row={target_root_row} dirty={ns.dirty_prompt_choice}"
        )

        verify_offset = len(serial_collector.snapshot())
        setattr(args, "overlay_load_verify_offset", verify_offset)

        recover_load_save_overlay(
            out_port,
            serial_collector,
            press_ms=ns.press_ms,
            gap_ms=ns.double_press_gap_ms,
            gesture_settle_ms=ns.gesture_settle_ms,
            send_double_press=_send_double_press,
            log_prefix=log_prefix,
        )

        print(f"{log_prefix} select track {ns.track_number}")
        _send_short_press(
            out_port,
            note=_track_select_note(ns.track_number),
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        time.sleep(ns.phase_wait_ms / 1000.0)

        if not ns.skip_workspace_save_prelude:
            if deferred_save_idle_in_tail(serial_collector.snapshot()):
                print(
                    f"{log_prefix} deferred save already idle — skipping transport prelude"
                )
                ns.skip_workspace_save_prelude = True
        if not ns.skip_workspace_save_prelude:
            print(
                f"{log_prefix} start transport (prelude — may take ~2 min for w0_s64 save)"
            )
            _ensure_transport_running(
                out_port,
                in_port,
                press_ms=ns.press_ms,
                phase_wait_ms=ns.phase_wait_ms,
            )
            time.sleep(ns.play_wait_ms / 1000.0)
            transport_stop_anchor = len(serial_collector.snapshot())
            print(f"{log_prefix} transport stop — flush current epoch before setup")
            _stop_transport_if_running(
                out_port,
                in_port,
                press_ms=ns.press_ms,
                phase_wait_ms=ns.phase_wait_ms,
            )
            if not wait_for_deferred_save_idle(
                serial_collector,
                after_line_index=transport_stop_anchor,
                timeout_s=save_drain_s,
                log_prefix=log_prefix,
            ):
                print(f"{log_prefix} error: deferred save did not finish after transport stop")
                return 2
            ctx.markers.append("phase:transport_stop_idle")

        if not ns.skip_setup_commit:
            setup_commit_anchor = len(serial_collector.snapshot())
            print(f"{log_prefix} serial !REV_COMMIT (ensure S{set_id:04d} revision on SD)")
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
            setattr(args, "expected_set_id", set_id)
            print(f"{log_prefix} setup revision S{set_id:04d} v{revision_id:04d}")
            ctx.markers.append("phase:setup_revision_commit")
            if set_id != ns.expected_set_id:
                print(
                    f"{log_prefix} warn: committed S{set_id:04d} "
                    f"!= expected S{ns.expected_set_id:04d}"
                )
        setattr(args, "expected_revision_id", revision_id)

        enter_anchor = len(serial_collector.snapshot())
        setattr(args, "overlay_load_enter_anchor", enter_anchor)

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
        dwell_ms(ns.overlay_root_dwell_ms, log_prefix, "root overlay open")

        scroll_anchor = midi_scroll_to_root_row(
            out_port,
            serial_collector,
            target_row=target_root_row,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
            phase_wait_ms=ns.phase_wait_ms,
            step_dwell_ms=ns.overlay_scroll_step_dwell_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
        )
        ctx.markers.append("phase:overlay_scrolled_to_set_row")

        confirm_anchor = len(serial_collector.snapshot())
        setattr(args, "overlay_load_confirm_anchor", confirm_anchor)

        midi_confirm_overlay_row(
            out_port,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
            gesture_settle_ms=ns.gesture_settle_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
            action_label=f"load Set row {target_root_row}",
        )
        ctx.markers.append("phase:overlay_confirm_load_midi")

        dirty_match = _wait_for_pattern_after(
            serial_collector,
            _REV_LOAD_DIRTY_PROMPT_RE,
            after_line_index=confirm_anchor,
            timeout_s=5.0,
            log_prefix=log_prefix,
            label="rev_load_dirty_prompt",
        )
        if dirty_match is not None:
            setattr(args, "expect_dirty_prompt", True)
            dirty_row = _DIRTY_PROMPT_ROW[ns.dirty_prompt_choice]
            dirty_scroll_anchor = midi_scroll_dirty_prompt_row(
                out_port,
                serial_collector,
                target_row=dirty_row,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
                phase_wait_ms=ns.phase_wait_ms,
                step_dwell_ms=ns.overlay_dirty_dwell_ms,
                send_short_press=_send_short_press,
                log_prefix=log_prefix,
                choice=ns.dirty_prompt_choice,
            )
            dirty_confirm_anchor = len(serial_collector.snapshot())
            setattr(args, "overlay_dirty_confirm_anchor", dirty_confirm_anchor)
            midi_confirm_overlay_row(
                out_port,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
                gesture_settle_ms=ns.gesture_settle_ms,
                send_short_press=_send_short_press,
                log_prefix=log_prefix,
                action_label=f"dirty prompt {ns.dirty_prompt_choice}",
            )
            ctx.markers.append(f"phase:dirty_prompt_{ns.dirty_prompt_choice}")
            if ns.dirty_prompt_choice == "cancel":
                print(f"{log_prefix} dirty prompt Cancel — load should not complete")
                return 0
            confirm_anchor = dirty_confirm_anchor

        if (
            _wait_for_pattern_after(
                serial_collector,
                _REV_LOAD_COMPLETE_RE,
                after_line_index=confirm_anchor,
                timeout_s=load_wait_s,
                log_prefix=log_prefix,
                label="rev_load_complete",
            )
            is None
        ):
            print(f"{log_prefix} error: timed out waiting for rev_load_complete")
            return 2

        print(f"{log_prefix} revision load completed via overlay MIDI")
        ctx.markers.append("phase:rev_load_complete")

        lines = serial_collector.snapshot()
        check = verify_load_save_overlay_load(lines, args)
        print(f"{log_prefix} serial verification ok={check.get('ok')}")
        for issue in check.get("issues", []):
            print(f"  issue: {issue}")

        out_dir = Path(getattr(args, "out_dir", Path("captures")))
        out_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        report_path = out_dir / f"host_midi_hitl_load_save_overlay_load_{stamp}.json"
        report_path.write_text(
            json.dumps(
                {
                    "scenario": "load_save_overlay_load",
                    "track_number": ns.track_number,
                    "expected_set_id": ns.expected_set_id,
                    "catalog_set_index": ns.catalog_set_index,
                    "target_root_row": target_root_row,
                    "committed_set_id": set_id,
                    "committed_revision_id": revision_id,
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
