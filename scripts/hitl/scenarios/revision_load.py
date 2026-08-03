"""Revision commit → load round-trip via HITL serial hooks, with optional catalog cleanup."""

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
from hitl.verify.revision_load import verify_revision_load

_REV_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_complete,\d+,\d+,\d+,S(\d{4})_v(\d{4})\b"
)
_REV_LOAD_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_complete,\d+,\d+,\d+,S\d{4}_v\d{4}\b"
)
_REV_CLEANUP_OK_RE = re.compile(r"#CAP,\d+,PERS,rev_cleanup,\d+,\d+,\d+,ok\b")
_REV_NUKE_SETS_OK_RE = re.compile(r"#CAP,\d+,PERS,rev_nuke_sets,\d+,\d+,\d+,ok\b")
_REV_NUKE_SETS_STARTED_RE = re.compile(r"#CAP,\d+,PERS,rev_nuke_sets,\d+,\d+,\d+,started\b")
_REVISION_JOB_FAILED_RE = re.compile(
    r"\[StorageManager\] ERROR: Revision (commit|load) failed|"
    r"\[StorageManager\] ERROR: Revision validate"
)

def _parse_common_args(args: object) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--track-number", "--track", type=int, default=5)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--play-wait-ms", type=int, default=1500)
    parser.add_argument("--prior-save-drain-wait-ms", type=int, default=120000)
    parser.add_argument("--deferred-save-wait-ms", type=int, default=120000)
    parser.add_argument("--revision-commit-wait-ms", type=int, default=180000)
    parser.add_argument("--revision-load-wait-ms", type=int, default=180000)
    parser.add_argument("--revision-cleanup-wait-ms", type=int, default=10000)
    parser.add_argument("--revision-nuke-wait-ms", type=int, default=120000)
    parser.add_argument(
        "--nuke-sets-before-run",
        action="store_true",
        default=False,
        help="Dev only: wipe /MidiLooper/sets/ before commit (synchronous SD — can stall USB)",
    )
    parser.add_argument(
        "--skip-sets-nuke",
        action="store_true",
        default=False,
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--skip-hitl-cleanup",
        action="store_true",
        default=False,
        help="Leave committed revision on SD (debug only)",
    )
    parser.add_argument(
        "--skip-workspace-save-prelude",
        action="store_true",
        default=False,
        help="Skip transport stop prelude (use after record baseline flushed loops to SD)",
    )
    legacy = list(getattr(args, "legacy_args", []) or [])
    known, _unknown = parser.parse_known_args(legacy)
    return known

def _track_select_note(track_number_1based: int) -> int:
    from hitl.control_constants import TRACK_SELECT_NOTE_BASE

    return TRACK_SELECT_NOTE_BASE + (track_number_1based - 1)

def _wait_for_pattern_after(
    collector,
    pattern: re.Pattern[str],
    *,
    after_line_index: int,
    timeout_s: float,
    log_prefix: str,
    label: str,
) -> Optional[re.Match[str]]:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    last_progress_log = 0.0
    while time.monotonic() < deadline:
        lines = collector.snapshot()
        for line in lines[max(0, after_line_index) :]:
            match = pattern.search(line)
            if match is not None:
                print(f"{log_prefix} saw {label}: {line}")
                return match
            if _REVISION_JOB_FAILED_RE.search(line):
                print(f"{log_prefix} error: firmware reported revision job failed")
                print(f"{log_prefix} serial: {line.strip()}")
                return None
            if "[StorageManager] ERROR:" in line:
                print(f"{log_prefix} serial: {line.strip()}")
        now = time.monotonic()
        if now - last_progress_log >= 8.0:
            pers_tail = [line for line in lines[-20:] if ",PERS," in line]
            print(
                f"{log_prefix} still waiting for {label} "
                f"({int(deadline - now)}s left) recent_pers={pers_tail[-3:]}"
            )
            last_progress_log = now
        time.sleep(0.05)
    return None

def _wait_for_serial_activity(
    collector,
    *,
    timeout_s: float,
    log_prefix: str,
) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        if collector.snapshot():
            return True
        time.sleep(0.1)
    print(f"{log_prefix} warn: no serial lines before nuke (continuing)")
    return False

def _request_sets_nuke(
    collector,
    *,
    after_line_index: int,
    timeout_s: float,
    log_prefix: str,
) -> bool:
    collector.write_line("!REV_NUKE_SETS")
    deadline = time.monotonic() + max(timeout_s, 0.0)
    last_progress_log = 0.0
    saw_started = False
    while time.monotonic() < deadline:
        lines = collector.snapshot()
        for line in lines[max(0, after_line_index) :]:
            if _REV_NUKE_SETS_STARTED_RE.search(line):
                saw_started = True
                print(f"{log_prefix} saw rev_nuke_sets started: {line}")
            if _REV_NUKE_SETS_OK_RE.search(line):
                print(f"{log_prefix} saw rev_nuke_sets ok: {line}")
                return True
            if _REVISION_JOB_FAILED_RE.search(line):
                print(f"{log_prefix} error: firmware reported revision job failed")
                print(f"{log_prefix} serial: {line.strip()}")
                return False
        now = time.monotonic()
        if now - last_progress_log >= 8.0:
            pers_tail = [line for line in lines[-20:] if ",PERS," in line]
            print(
                f"{log_prefix} still waiting for rev_nuke_sets ok "
                f"({int(deadline - now)}s left) started={saw_started} "
                f"recent_pers={pers_tail[-3:]}"
            )
            last_progress_log = now
        time.sleep(0.05)
    return False

def run_revision_load(args: object) -> int:
    from hitl.edit_controls import (
        _ensure_transport_running,
        _stop_transport_if_running,
    )
    from hitl.control_constants import CONTROL_CHANNEL_1BASED
    from hitl.serial_collector import SerialCaptureCollector
    from hitl.midi_io import (
        _drain_input_messages,
        _find_midi_port,
        _send_short_press,
    )

    ns = _parse_common_args(args)
    skip_prelude = bool(getattr(args, "skip_workspace_save_prelude", False)) or ns.skip_workspace_save_prelude
    # Default path: commit → load → cleanup only. Nuke is opt-in (blocks main loop on SD).
    skip_nuke = not ns.nuke_sets_before_run or ns.skip_sets_nuke
    setattr(args, "require_sets_nuke", not skip_nuke)
    setattr(args, "require_hitl_cleanup", not ns.skip_hitl_cleanup)
    ctx = get_context(args)
    exit_code = 0
    log_prefix = "[revision-load-hitl]"
    prior_drain_s = ns.prior_save_drain_wait_ms / 1000.0
    save_drain_s = ns.deferred_save_wait_ms / 1000.0
    commit_wait_s = ns.revision_commit_wait_ms / 1000.0
    load_wait_s = ns.revision_load_wait_ms / 1000.0
    cleanup_wait_s = ns.revision_cleanup_wait_ms / 1000.0
    nuke_wait_s = ns.revision_nuke_wait_ms / 1000.0

    import mido

    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    serial_collector: Optional[SerialCaptureCollector] = None
    nuke_anchor = -1
    commit_anchor = 0
    load_anchor = -1
    cleanup_anchor = -1

    try:
        if not ns.serial_port:
            print(f"{log_prefix} error: --serial-port is required")
            return 2

        serial_collector = SerialCaptureCollector(ns.serial_port, baud=ns.serial_baud)
        serial_collector.start()
        time.sleep(0.3)
        serial_start_index = len(serial_collector.snapshot())

        if serial_start_index < 8:
            print(f"{log_prefix} skip prior deferred-save drain (fresh serial session)")
        else:
            print(
                f"{log_prefix} drain deferred save from any prior run "
                f"(up to {prior_drain_s:.0f}s)"
            )
            if not wait_for_deferred_save_idle(
                serial_collector,
                after_line_index=serial_start_index,
                timeout_s=prior_drain_s,
                log_prefix=log_prefix,
            ):
                print(f"{log_prefix} warn: prior deferred save may still be active")

        print(f"{log_prefix} select track {ns.track_number} (1-based, same as base preset)")
        _send_short_press(
            out_port,
            note=_track_select_note(ns.track_number),
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        time.sleep(ns.phase_wait_ms / 1000.0)

        if skip_prelude:
            print(
                f"{log_prefix} skip workspace save prelude "
                "(record baseline already flushed loops to SD)"
            )
            transport_stop_anchor = len(serial_collector.snapshot())
            print(
                f"{log_prefix} skip deferred-save drain before commit "
                "(base preset already reported PERS,result,ok)"
            )
        else:
            print(f"{log_prefix} start transport")
            _ensure_transport_running(
                out_port,
                in_port,
                press_ms=ns.press_ms,
                phase_wait_ms=ns.phase_wait_ms,
            )
            print(f"{log_prefix} short play ({ns.play_wait_ms}ms)")
            time.sleep(ns.play_wait_ms / 1000.0)

            print(f"{log_prefix} transport stop (flush current epoch to SD)")
            transport_stop_anchor = len(serial_collector.snapshot())
            _stop_transport_if_running(
                out_port,
                in_port,
                press_ms=ns.press_ms,
                phase_wait_ms=ns.phase_wait_ms,
            )
            ctx.markers.append("phase:transport_stop_idle")
            if not wait_for_deferred_save_idle(
                serial_collector,
                after_line_index=transport_stop_anchor,
                timeout_s=save_drain_s,
                log_prefix=log_prefix,
            ):
                print(f"{log_prefix} error: deferred save did not finish after transport stop")
                return 2
            print(f"{log_prefix} current workspace epoch saved")
        time.sleep(ns.phase_wait_ms / 1000.0)

        if not skip_nuke:
            _wait_for_serial_activity(
                serial_collector,
                timeout_s=15.0,
                log_prefix=log_prefix,
            )
            print(f"{log_prefix} serial !REV_NUKE_SETS (remove failed/orphan S#### folders)")
            nuke_anchor = len(serial_collector.snapshot())
            setattr(args, "revision_nuke_serial_anchor", nuke_anchor)
            ctx.markers.append("phase:revision_sets_nuke")
            if not _request_sets_nuke(
                serial_collector,
                after_line_index=nuke_anchor,
                timeout_s=nuke_wait_s,
                log_prefix=log_prefix,
            ):
                print(f"{log_prefix} error: timed out waiting for rev_nuke_sets ok")
                return 2
            print(f"{log_prefix} sets catalog nuke completed")
            time.sleep(ns.phase_wait_ms / 1000.0)

        print(f"{log_prefix} serial !REV_COMMIT (create revision to load)")
        commit_anchor = len(serial_collector.snapshot())
        setattr(args, "revision_commit_serial_anchor", commit_anchor)
        serial_collector.write_line("!REV_COMMIT")
        ctx.markers.append("phase:revision_commit_request")

        commit_match = _wait_for_pattern_after(
            serial_collector,
            _REV_COMPLETE_RE,
            after_line_index=commit_anchor,
            timeout_s=commit_wait_s,
            log_prefix=log_prefix,
            label="rev_complete",
        )
        if commit_match is None:
            print(f"{log_prefix} error: timed out waiting for rev_complete")
            return 2

        set_id = int(commit_match.group(1))
        revision_id = int(commit_match.group(2))
        print(f"{log_prefix} revision commit completed S{set_id:04d} v{revision_id:04d}")

        load_command = f"!REV_LOAD {set_id} {revision_id}"
        print(f"{log_prefix} serial {load_command} (deferred load into new Current epoch)")
        load_anchor = len(serial_collector.snapshot())
        setattr(args, "revision_load_serial_anchor", load_anchor)
        serial_collector.write_line(load_command)
        ctx.markers.append("phase:revision_load_request")

        if (
            _wait_for_pattern_after(
                serial_collector,
                _REV_LOAD_COMPLETE_RE,
                after_line_index=load_anchor,
                timeout_s=load_wait_s,
                log_prefix=log_prefix,
                label="rev_load_complete",
            )
            is None
        ):
            print(f"{log_prefix} error: timed out waiting for rev_load_complete")
            return 2
        print(f"{log_prefix} revision load completed")

        if not ns.skip_hitl_cleanup:
            print(f"{log_prefix} serial !REV_CLEANUP (restore catalog / remove revision)")
            cleanup_anchor = len(serial_collector.snapshot())
            setattr(args, "revision_cleanup_serial_anchor", cleanup_anchor)
            serial_collector.write_line("!REV_CLEANUP")
            ctx.markers.append("phase:revision_cleanup_request")
            if (
                _wait_for_pattern_after(
                    serial_collector,
                    _REV_CLEANUP_OK_RE,
                    after_line_index=cleanup_anchor,
                    timeout_s=cleanup_wait_s,
                    log_prefix=log_prefix,
                    label="rev_cleanup ok",
                )
                is None
            ):
                print(f"{log_prefix} error: timed out waiting for rev_cleanup ok")
            else:
                print(f"{log_prefix} revision cleanup completed")

        lines = serial_collector.snapshot()
        check = verify_revision_load(lines, args)
        print(f"{log_prefix} serial verification ok={check.get('ok')}")
        for issue in check.get("issues", []):
            print(f"  issue: {issue}")

        out_dir = Path(getattr(args, "out_dir", Path("captures")))
        out_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        report_path = out_dir / f"host_midi_hitl_revision_load_{stamp}.json"
        report_path.write_text(
            json.dumps(
                {
                    "scenario": "revision_load",
                    "track_number": ns.track_number,
                    "nuke_anchor": nuke_anchor,
                    "commit_anchor": commit_anchor,
                    "load_anchor": load_anchor,
                    "cleanup_anchor": cleanup_anchor,
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
