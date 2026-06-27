"""Dirty-prompt revision load HITL (Yes / No / Cancel after record makes workspace dirty)."""

from __future__ import annotations

import json
import re
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

from hitl.context import get_context
from hitl.deferred_save_idle import wait_for_deferred_save_idle
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
_REVISION_JOB_FAILED_RE = re.compile(
    r"\[StorageManager\] ERROR: Revision (commit|load) failed|"
    r"\[StorageManager\] ERROR: Revision validate"
)

_DIRTY_COMMANDS = {
    "yes": "!REV_LOAD_DIRTY_YES",
    "no": "!REV_LOAD_DIRTY_NO",
    "cancel": "!REV_LOAD_DIRTY_CANCEL",
}


def _track_select_note(track_number_1based: int) -> int:
    from host_midi_automation_baseline import TRACK_SELECT_NOTE_BASE

    return TRACK_SELECT_NOTE_BASE + (track_number_1based - 1)


def _make_workspace_dirty_again(
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
    from host_midi_automation_baseline import (
        CONTROL_CHANNEL_1BASED,
        RECORD_BUTTON_NOTE,
        MIDI_CLOCKS_PER_BAR,
        _send_short_press,
        _wait_for_clock_pulses,
    )
    from host_midi_automation_edit_baseline import _ensure_transport_running

    print(f"{log_prefix} make workspace dirty: {record_bars}-bar record on track {track_number}")
    _ensure_transport_running(
        out_port,
        in_port,
        press_ms=press_ms,
        phase_wait_ms=phase_wait_ms,
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
    seen = _wait_for_clock_pulses(
        in_port,
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

    from host_midi_automation_edit_baseline import _stop_transport_if_running

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


def run_revision_load_dirty(args: object) -> int:
    from host_midi_automation_baseline import (
        CONTROL_CHANNEL_1BASED,
        SerialCaptureCollector,
        _drain_input_messages,
        _find_midi_port,
        _send_short_press,
    )

    choice = str(getattr(args, "dirty_prompt_choice", "") or "").lower()
    if choice not in _DIRTY_COMMANDS:
        print(f"[revision-load-dirty-hitl] error: unknown dirty_prompt_choice={choice!r}")
        return 2

    ns = _parse_common_args(args)
    setattr(args, "skip_workspace_save_prelude", True)
    setattr(args, "require_sets_nuke", False)
    setattr(args, "require_hitl_cleanup", False)
    ctx = get_context(args)
    log_prefix = f"[revision-load-dirty-{choice}-hitl]"
    save_drain_s = ns.deferred_save_wait_ms / 1000.0
    commit_wait_s = ns.revision_commit_wait_ms / 1000.0
    load_wait_s = ns.revision_load_wait_ms / 1000.0
    dirty_command = _DIRTY_COMMANDS[choice]
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
        time.sleep(0.3)

        print(f"{log_prefix} select track {ns.track_number}")
        _send_short_press(
            out_port,
            note=_track_select_note(ns.track_number),
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        time.sleep(ns.phase_wait_ms / 1000.0)

        setup_commit_anchor = len(serial_collector.snapshot())
        setattr(args, "revision_setup_commit_anchor", setup_commit_anchor)
        print(f"{log_prefix} serial !REV_COMMIT (setup revision on SD)")
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

        load_command = f"!REV_LOAD {set_id} {revision_id}"
        load_anchor = len(serial_collector.snapshot())
        setattr(args, "revision_load_serial_anchor", load_anchor)
        print(f"{log_prefix} serial {load_command} (expect dirty prompt)")
        serial_collector.write_line(load_command)
        ctx.markers.append("phase:revision_load_request")

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

        dirty_choice_anchor = len(serial_collector.snapshot())
        setattr(args, "revision_dirty_choice_anchor", dirty_choice_anchor)
        print(f"{log_prefix} serial {dirty_command}")
        serial_collector.write_line(dirty_command)
        ctx.markers.append(f"phase:dirty_prompt_{choice}")

        if choice == "cancel":
            time.sleep(2.0)
        elif choice == "no":
            if (
                _wait_for_pattern_after(
                    serial_collector,
                    _REV_LOAD_COMPLETE_RE,
                    after_line_index=dirty_choice_anchor,
                    timeout_s=load_wait_s,
                    log_prefix=log_prefix,
                    label="rev_load_complete",
                )
                is None
            ):
                print(f"{log_prefix} error: timed out waiting for rev_load_complete")
                return 2
            print(f"{log_prefix} discard-load completed")
        else:
            second_commit = _wait_for_pattern_after(
                serial_collector,
                _REV_COMPLETE_RE,
                after_line_index=dirty_choice_anchor,
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
                    after_line_index=dirty_choice_anchor,
                    timeout_s=load_wait_s,
                    log_prefix=log_prefix,
                    label="rev_load_complete",
                )
                is None
            ):
                print(f"{log_prefix} error: timed out waiting for rev_load_complete after dirty yes")
                return 2
            print(f"{log_prefix} save-then-load completed")

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
                    "track_number": ns.track_number,
                    "setup_commit_anchor": setup_commit_anchor,
                    "dirty_record_anchor": dirty_record_anchor,
                    "load_anchor": load_anchor,
                    "dirty_choice_anchor": dirty_choice_anchor,
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
