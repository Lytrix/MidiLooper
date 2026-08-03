"""Dirty-prompt revision load HITL via load/save overlay MIDI (watchable on OLED)."""

from __future__ import annotations

import json
import re
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

from hitl.context import get_context
from hitl.scenarios.load_save_overlay_helpers import (
    DIRTY_PROMPT_ROW,
    dwell_ms,
    enter_load_save_overlay,
    exit_load_save_overlay,
    midi_confirm_overlay_row,
    midi_scroll_dirty_prompt_row,
    midi_scroll_to_root_row,
    overlay_scroll_steps_to_set_row,
    parse_overlay_watch_args,
    recover_load_save_overlay,
    make_workspace_dirty_again,
    wait_serial_cap_ready,
)
from hitl.scenarios.revision_load import _parse_common_args, _wait_for_pattern_after
from hitl.verify.revision_load_dirty import verify_revision_load_dirty

_REV_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_complete,\d+,\d+,\d+,S(\d{4})_v(\d{4})\b"
)
_REV_LOAD_DIRTY_PROMPT_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_dirty_prompt,\d+,\d+,\d+,shown\b"
)
_REV_LOAD_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_complete,\d+,\d+,\d+,S\d{4}_v\d{4}\b"
)

def _track_select_note(track_number_1based: int) -> int:
    from hitl.control_constants import TRACK_SELECT_NOTE_BASE

    return TRACK_SELECT_NOTE_BASE + (track_number_1based - 1)

def run_revision_load_dirty(args: object) -> int:
    from hitl.control_constants import CONTROL_CHANNEL_1BASED
    from hitl.serial_collector import SerialCaptureCollector
    from hitl.midi_io import (
        _drain_input_messages,
        _find_midi_port,
        _send_short_press,
    )
    from hitl.edit_controls import _send_double_press

    choice = str(getattr(args, "dirty_prompt_choice", "") or "").lower()
    if choice not in DIRTY_PROMPT_ROW:
        print(f"[revision-load-dirty-hitl] error: unknown dirty_prompt_choice={choice!r}")
        return 2

    rev_ns = _parse_common_args(args)
    watch_ns = parse_overlay_watch_args(args)
    setattr(args, "skip_workspace_save_prelude", True)
    setattr(args, "require_sets_nuke", False)
    setattr(args, "require_hitl_cleanup", False)
    ctx = get_context(args)
    log_prefix = f"[revision-load-dirty-{choice}-hitl]"
    save_drain_s = rev_ns.deferred_save_wait_ms / 1000.0
    commit_wait_s = rev_ns.revision_commit_wait_ms / 1000.0
    load_wait_s = rev_ns.revision_load_wait_ms / 1000.0
    record_bars = int(getattr(args, "record_bars", None) or 2)
    catalog_set_index = int(getattr(args, "catalog_set_index", watch_ns.catalog_set_index))
    target_root_row = overlay_scroll_steps_to_set_row(catalog_set_index)
    dirty_row = DIRTY_PROMPT_ROW[choice]

    import mido

    out_port = mido.open_output(_find_midi_port(watch_ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(watch_ns.midi_in, "input"))
    serial_collector: Optional[SerialCaptureCollector] = None
    exit_code = 0

    try:
        if not watch_ns.serial_port:
            print(f"{log_prefix} error: --serial-port is required")
            return 2

        serial_collector = SerialCaptureCollector(watch_ns.serial_port, baud=watch_ns.serial_baud)
        serial_collector.start()
        time.sleep(1.0)
        if not wait_serial_cap_ready(serial_collector):
            print(f"{log_prefix} warn: serial CAP not ready yet")

        print(
            f"{log_prefix} plan: overlay MIDI load + dirty={choice} "
            f"row={target_root_row} dwell={watch_ns.overlay_scroll_step_dwell_ms}ms"
        )

        recover_load_save_overlay(
            out_port,
            serial_collector,
            press_ms=watch_ns.press_ms,
            gap_ms=watch_ns.double_press_gap_ms,
            gesture_settle_ms=watch_ns.gesture_settle_ms,
            send_double_press=_send_double_press,
            log_prefix=log_prefix,
        )

        print(f"{log_prefix} select track {watch_ns.track_number}")
        _send_short_press(
            out_port,
            note=_track_select_note(watch_ns.track_number),
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=watch_ns.press_ms,
        )
        time.sleep(watch_ns.phase_wait_ms / 1000.0)

        setup_commit_anchor = len(serial_collector.snapshot())
        setattr(args, "revision_setup_commit_anchor", setup_commit_anchor)
        print(f"{log_prefix} serial !REV_COMMIT (setup revision on SD only)")
        serial_collector.write_line("!REV_COMMIT")
        ctx.markers.append("phase:setup_revision_commit")

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
        print(f"{log_prefix} setup revision S{set_id:04d} v{revision_id:04d}")

        dirty_record_anchor = len(serial_collector.snapshot())
        setattr(args, "revision_dirty_record_anchor", dirty_record_anchor)
        if not make_workspace_dirty_again(
            out_port,
            in_port,
            serial_collector,
            track_number=watch_ns.track_number,
            record_bars=record_bars,
            press_ms=watch_ns.press_ms,
            phase_wait_ms=watch_ns.phase_wait_ms,
            save_drain_s=save_drain_s,
            log_prefix=log_prefix,
        ):
            return 2
        ctx.markers.append("phase:dirty_record")

        if not enter_load_save_overlay(
            out_port,
            serial_collector,
            press_ms=watch_ns.press_ms,
            gap_ms=watch_ns.double_press_gap_ms,
            gesture_settle_ms=watch_ns.gesture_settle_ms,
            send_double_press=_send_double_press,
            log_prefix=log_prefix,
        ):
            return 2
        ctx.markers.append("phase:overlay_enter_midi")
        dwell_ms(watch_ns.overlay_root_dwell_ms, log_prefix, "root overlay open")

        scroll_anchor = midi_scroll_to_root_row(
            out_port,
            serial_collector,
            target_row=target_root_row,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=watch_ns.press_ms,
            phase_wait_ms=watch_ns.phase_wait_ms,
            step_dwell_ms=watch_ns.overlay_scroll_step_dwell_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
        )
        ctx.markers.append("phase:overlay_scrolled_to_set_row")

        confirm_anchor = len(serial_collector.snapshot())
        setattr(args, "overlay_load_confirm_anchor", confirm_anchor)
        setattr(args, "revision_load_serial_anchor", confirm_anchor)
        midi_confirm_overlay_row(
            out_port,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=watch_ns.press_ms,
            gesture_settle_ms=watch_ns.gesture_settle_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
            action_label=f"load Set row {target_root_row} (expect dirty prompt)",
        )
        ctx.markers.append("phase:overlay_confirm_load_midi")

        if (
            _wait_for_pattern_after(
                serial_collector,
                _REV_LOAD_DIRTY_PROMPT_RE,
                after_line_index=confirm_anchor,
                timeout_s=30.0,
                log_prefix=log_prefix,
                label="rev_load_dirty_prompt",
            )
            is None
        ):
            print(f"{log_prefix} error: timed out waiting for rev_load_dirty_prompt")
            return 2
        ctx.markers.append("phase:dirty_prompt_shown")
        dwell_ms(watch_ns.overlay_dirty_dwell_ms, log_prefix, "dirty prompt visible")

        dirty_scroll_anchor = midi_scroll_dirty_prompt_row(
            out_port,
            serial_collector,
            target_row=dirty_row,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=watch_ns.press_ms,
            phase_wait_ms=watch_ns.phase_wait_ms,
            step_dwell_ms=watch_ns.overlay_dirty_dwell_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
            choice=choice,
        )

        dirty_confirm_anchor = len(serial_collector.snapshot())
        setattr(args, "revision_dirty_choice_anchor", dirty_confirm_anchor)
        setattr(args, "overlay_dirty_confirm_anchor", dirty_confirm_anchor)
        midi_confirm_overlay_row(
            out_port,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=watch_ns.press_ms,
            gesture_settle_ms=watch_ns.gesture_settle_ms,
            send_short_press=_send_short_press,
            log_prefix=log_prefix,
            action_label=f"dirty prompt {choice}",
        )
        ctx.markers.append(f"phase:dirty_prompt_{choice}")

        if choice == "cancel":
            time.sleep(2.0)
            exit_load_save_overlay(
                out_port,
                serial_collector,
                press_ms=watch_ns.press_ms,
                gap_ms=watch_ns.double_press_gap_ms,
                gesture_settle_ms=watch_ns.gesture_settle_ms,
                send_double_press=_send_double_press,
                log_prefix=log_prefix,
            )
        elif choice == "no":
            if (
                _wait_for_pattern_after(
                    serial_collector,
                    _REV_LOAD_COMPLETE_RE,
                    after_line_index=dirty_confirm_anchor,
                    timeout_s=load_wait_s,
                    log_prefix=log_prefix,
                    label="rev_load_complete",
                )
                is None
            ):
                print(f"{log_prefix} error: timed out waiting for rev_load_complete")
                return 2
            print(f"{log_prefix} discard-load completed via overlay")
        else:
            second_commit = _wait_for_pattern_after(
                serial_collector,
                _REV_COMPLETE_RE,
                after_line_index=dirty_confirm_anchor,
                timeout_s=commit_wait_s,
                log_prefix=log_prefix,
                label="save-then-load rev_complete",
            )
            if second_commit is None:
                print(f"{log_prefix} error: timed out waiting for save-then-load rev_complete")
                return 2
            if (
                _wait_for_pattern_after(
                    serial_collector,
                    _REV_LOAD_COMPLETE_RE,
                    after_line_index=dirty_confirm_anchor,
                    timeout_s=load_wait_s,
                    log_prefix=log_prefix,
                    label="rev_load_complete",
                )
                is None
            ):
                print(f"{log_prefix} error: timed out waiting for rev_load_complete after dirty yes")
                return 2
            print(f"{log_prefix} save-then-load completed via overlay")

        lines = serial_collector.snapshot()
        check = verify_revision_load_dirty(lines, args)
        print(f"{log_prefix} serial verification ok={check.get('ok')}")
        for issue in check.get("issues", []):
            print(f"  issue: {issue}")

        out_dir = Path(getattr(args, "out_dir", Path("captures")))
        out_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        report_path = out_dir / f"host_midi_hitl_revision_load_dirty_{choice}_{stamp}.json"
        report_path.write_text(
            json.dumps(
                {
                    "scenario": f"revision_load_dirty_{choice}",
                    "dirty_prompt_choice": choice,
                    "track_number": watch_ns.track_number,
                    "overlay_scroll_step_dwell_ms": watch_ns.overlay_scroll_step_dwell_ms,
                    "setup_set_id": set_id,
                    "setup_revision_id": revision_id,
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

def run_revision_load_dirty_yes(args: object) -> int:
    setattr(args, "dirty_prompt_choice", "yes")
    return run_revision_load_dirty(args)

def run_revision_load_dirty_no(args: object) -> int:
    setattr(args, "dirty_prompt_choice", "no")
    return run_revision_load_dirty(args)

def run_revision_load_dirty_cancel(args: object) -> int:
    setattr(args, "dirty_prompt_choice", "cancel")
    return run_revision_load_dirty(args)
