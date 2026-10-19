"""Nav inventory for loops seeded by the base HITL preset (record + 2 overdub passes)."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Mapping

from hitl.control_constants import (
    MIDI_CLOCKS_PER_BAR,
    OVERDUB_GRID_STEP_CLOCKS,
    RECORD_GRID_STEP_CLOCKS,
    TICKS_PER_16TH_STEP,
    TICKS_PER_BAR,
)
from host_midi_automation_baseline import (
    _extract_phase_boundaries,
    _extract_sevt_events,
)
from host_midi_automation_edit_baseline import (
    RecordLayout,
    _build_select_navigation_slots,
    _extract_last_recs_stop,
    _extract_revt_notes,
)


def latest_base_report(captures_dir: Path) -> dict[str, Any] | None:
    reports = sorted(
        captures_dir.glob("host_midi_automation_baseline_*.json"),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    for report_path in reports:
        try:
            return json.loads(report_path.read_text(encoding="utf-8"))
        except (OSError, ValueError, json.JSONDecodeError):
            continue
    return None

def serial_log_from_base_report(report: Mapping[str, Any]) -> Path | None:
    serial_path = report.get("serial_log_path")
    if not serial_path and base_preset_config(report).get("record_only"):
        serial_path = base_preset_config(report).get("follow_serial_log")
    if serial_path and Path(serial_path).is_file():
        return Path(serial_path)
    return None


def current_session_serial_log(out_dir: Path) -> Path | None:
    pointer = out_dir / ".current_session"
    if not pointer.is_file():
        return None
    raw = pointer.read_text(encoding="utf-8").strip()
    if not raw:
        return None
    path = Path(raw)
    if not path.is_absolute():
        path = (Path.cwd() / path).resolve()
    if path.is_file():
        return path
    return None


def resolve_base_seed_serial_log(
    report: Mapping[str, Any] | None,
    out_dir: Path,
    *,
    explicit: Path | None = None,
) -> Path | None:
    """Prefer the baseline snapshot file; follow-current-session uses `.current_session`."""
    if explicit is not None and explicit.is_file():
        return explicit
    if report is not None:
        from_report = serial_log_from_base_report(report)
        if from_report is not None:
            return from_report
    return current_session_serial_log(out_dir)

def base_preset_config(report: Mapping[str, Any] | None) -> dict[str, Any]:
    if report is None:
        return {}
    config = report.get("config")
    return dict(config) if isinstance(config, dict) else {}

def base_report_ok(report: Mapping[str, Any] | None) -> bool:
    """True when host_midi_automation_baseline JSON reports overall_ok."""
    if report is None:
        return False
    return bool(report.get("overall_ok"))

def base_report_loop_materialized(report: Mapping[str, Any] | None) -> bool:
    """True when base produced a 2+2 overdub loop usable for NOTE_EDIT fader sweep.

    Core HITL-Test-Flow gates (record/overdub clocks, transitions) without SEVT span
  or undo/redo log strings — those may lag deferred save on fast runs.
    """
    if report is None:
        return False
    stats = report.get("per_track_stats") or []
    if not stats:
        return False
    row = stats[0]
    if int(row.get("record_notes_sent", 0) or 0) < 32:
        return False
    if int(row.get("record_clock_pulses_seen", 0) or 0) < 192:
        return False
    if int(row.get("overdub_notes_sent", 0) or 0) < 16:
        return False
    if int(row.get("overdub_clock_pulses_seen", 0) or 0) < 192:
        return False
    config = report.get("config") or {}
    if int(config.get("second_overdub_bars", 0) or 0) > 0:
        if int(row.get("second_overdub_clock_pulses_seen", 0) or 0) < 192:
            return False
        if int(row.get("second_overdub_notes_sent", 0) or 0) < 7:
            return False
    transitions = (report.get("assertions") or {}).get("transition_checks") or []
    for transition in transitions:
        if transition.get("ok") is False:
            return False
    return True

def record_seed_qualification_issues(report: Mapping[str, Any] | None) -> list[str]:
    """Record-only admission, independent of the baseline's host-grid verdict.

    Playback is checked freshly by the edit leg. Capture, committed content and
    canonical cleanup are never waived by that later guard or by overall_ok.
    """
    if not isinstance(report, Mapping):
        return ["record seed report missing"]
    issues: list[str] = []
    config = base_preset_config(report)
    assertions = report.get("assertions") or {}
    hitl = report.get("hitl_report") or {}
    if hitl.get("capture_complete") is not True or hitl.get("capture_integrity") != "COMPLETE":
        issues.append("record seed capture incomplete or missing")
    if hitl.get("stimulus_valid") is not True:
        issues.append("record seed stimulus unqualified")
    if (assertions.get("serial_capture_enabled") is not True
            or assertions.get("serial_error") != ""
            or not isinstance(assertions.get("verification_line_count"), int)
            or assertions.get("verification_line_count", 0) <= 0):
        issues.append("record seed serial capture metadata incomplete")
    if (assertions.get("abort_reason") not in ("", None)
            or "abort_reason" not in assertions
            or assertions.get("timed_out") is not False
            or assertions.get("heartbeat_lost") is not False):
        issues.append(f"record seed aborted or run completion missing: {assertions.get('abort_reason')}")
    for key in ("precondition_failures", "phase_note_failures", "phase_activation_failures"):
        if assertions.get(key) != []:
            issues.append(f"record seed {key} failed or missing")
    transitions = assertions.get("transition_checks") or []
    if not any(t.get("to") == "RECORDING" and t.get("ok") is True for t in transitions):
        issues.append("record seed recording entry unconfirmed")
    if not any(t.get("from") == "RECORDING" and t.get("to") == "STOPPED_RECORDING"
               and t.get("ok") is True for t in transitions):
        issues.append("record seed stop unconfirmed")
    drain = assertions.get("record_evidence_drain") or {}
    if drain.get("complete") is not True or drain.get("stopped_confirmed") is not True:
        issues.append(f"record evidence drain incomplete: {drain.get('reason', 'missing completion')}")
    shutdown = drain.get("persistence_shutdown") or {}
    if (shutdown.get("status") != "complete" or shutdown.get("ok") is not True
            or not isinstance(shutdown.get("drains"), int) or shutdown.get("drains", 0) < 2):
        issues.append("record seed canonical persistence shutdown incomplete (requires two drains)")
    verification = assertions.get("serial_verification") or {}
    if verification.get("cap_sparse") is not False:
        issues.append("record seed serial verification missing or sparse")
    bars = config.get("record_bars")
    intended_length = bars * TICKS_PER_BAR if isinstance(bars, int) and bars > 0 else None
    length = verification.get("record_loop_length") or {}
    if (intended_length is None or length.get("recs_missing") is not False
            or length.get("actual_final_length") != intended_length):
        issues.append(f"record seed committed length {length.get('actual_final_length')} != intended {intended_length}")
    boundaries = verification.get("boundaries") or {}
    start, stop = boundaries.get("record_start_ts"), boundaries.get("record_stop_ts")
    window_ok = isinstance(start, int) and isinstance(stop, int) and stop > start
    if not window_ok:
        issues.append("record seed phase boundaries missing")
    handoff = verification.get("persistence_handoff") or {}
    result = handoff.get("result") or {}
    if (handoff.get("result_ok") is not True or handoff.get("first_missing_stage", "missing") is not None
            or result.get("outcome") != "ok" or not window_ok
            or not isinstance(result.get("timestamp"), int) or result.get("timestamp", 0) <= stop):
        issues.append("record seed current persistence result missing or unsuccessful")
    from host_midi_automation_edit_baseline import EDIT_RECORD_FIXTURE
    fixture = bool(config.get("edit_record_fixture"))
    expected_count = (len(EDIT_RECORD_FIXTURE) if fixture else
                      (bars * MIDI_CLOCKS_PER_BAR // RECORD_GRID_STEP_CLOCKS if intended_length else None))
    stats = report.get("per_track_stats") or []
    if not stats or stats[0].get("record_notes_sent") != expected_count:
        issues.append(f"record seed host fixture inventory missing or mismatched (expected {expected_count})")
    phase = verification.get("record_phase") or {}
    if (phase.get("phase_missing") is not False or expected_count is None
            or phase.get("note_on_count") != expected_count
            or phase.get("note_off_count") != expected_count
            or phase.get("unmatched_open_notes") != 0 or phase.get("out_of_range_count") != 0):
        issues.append(f"record seed serial On/Off inventory incomplete (expected {expected_count})")
    # The generic ascending-pitch/grid verifier does not describe EDIT_RECORD_FIXTURE.
    # Check its stored pitches using the existing Record parser, not a relaxed minimum.
    if fixture:
        path = serial_log_from_base_report(report)
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines() if path else []
            # Do not borrow RECA/RECS/REVT from a predecessor take in a followed log.
            current_lines = []
            for line in lines:
                parts = line.split(",")
                if line.startswith("#CAP,") and len(parts) >= 3:
                    try:
                        timestamp = int(parts[1])
                    except ValueError:
                        continue
                    if window_ok and timestamp >= start:
                        current_lines.append(line)
            recs = _extract_last_recs_stop(current_lines)
            if recs is None or recs["final_length"] != intended_length:
                issues.append("record seed serial committed length missing or mismatched")
            channel = config.get("midi_channel")
            if not isinstance(channel, int) or not 1 <= channel <= 16:
                issues.append("record seed MIDI channel missing or invalid")
            # When ingress rows are available, qualify Off identities as well as
            # On identities; balanced counts alone can conceal wrong-pitch Offs.
            ingress = {144: [], 128: []}
            for line in current_lines:
                if ",MI,U," not in line:
                    continue
                parts = line.split(",")
                if len(parts) < 8:
                    continue
                timestamp, kind, ch, pitch = map(int, (parts[1], parts[4], parts[5], parts[6]))
                if timestamp < stop and ch == channel and kind in ingress:
                    ingress[kind].append(pitch)
            expected_pitches = sorted(n.pitch for n in EDIT_RECORD_FIXTURE)
            if any(ingress.values()) and any(sorted(pitches) != expected_pitches for pitches in ingress.values()):
                issues.append("record seed serial EDIT_RECORD_FIXTURE On/Off pitches mismatched")
            notes = _extract_revt_notes(current_lines)
            if (sorted(pitch for _tick, pitch in notes) != sorted(n.pitch for n in EDIT_RECORD_FIXTURE)
                    or intended_length is None
                    or any(tick < 0 or tick >= intended_length for tick, _pitch in notes)):
                issues.append("record seed stored EDIT_RECORD_FIXTURE content missing or mismatched")
        except (OSError, ValueError, TypeError):
            issues.append("record seed stored fixture serial evidence unreadable")
    elif phase.get("sequence_mismatch_count") != 0:
        issues.append("record seed serial pitch sequence mismatched or missing")
    return issues

def base_report_record_seed_ok(report: Mapping[str, Any] | None) -> bool:
    """Qualify Record-only seeds; preserve full baseline legacy admission."""
    if base_preset_config(report).get("record_only"):
        return not record_seed_qualification_issues(report)
    if report is None:
        return False
    stats = report.get("per_track_stats") or []
    if not stats:
        return False
    row = stats[0]
    config = base_preset_config(report)
    min_notes = 7 if config.get("edit_record_fixture") else 32
    if int(row.get("record_notes_sent", 0) or 0) < min_notes:
        return False
    # Fixture seed is wall-clock paced and sends 8 notes. Host USB MIDI in often
    # misses Teensy clock, so do not require the 32-note 192-pulse grid count.
    if not config.get("edit_record_fixture"):
        if int(row.get("record_clock_pulses_seen", 0) or 0) < 192:
            return False
    transitions = (report.get("assertions") or {}).get("transition_checks") or []
    if config.get("record_only"):
        # Edit-minimal seed: capture + fixture notes matter more than every ST gate.
        for transition in transitions:
            key = (transition.get("from"), transition.get("to"))
            if key == ("RECORDING", "STOPPED_RECORDING") and transition.get("ok") is False:
                return False
    else:
        for transition in transitions:
            if transition.get("ok") is False:
                return False
    return True

def base_report_usable_for_note_edit_sweep(report: Mapping[str, Any] | None) -> bool:
    config = base_preset_config(report)
    if config.get("record_only"):
        return base_report_record_seed_ok(report)
    if base_report_ok(report):
        return True
    return base_report_loop_materialized(report)

def _phase_clock_to_storage_tick(phase_clock: int) -> int:
    return phase_clock * TICKS_PER_BAR // MIDI_CLOCKS_PER_BAR

def overdub_pass_pairs_from_base_config(
    config: Mapping[str, Any],
    *,
    pass_key: str,
    loop_length: int,
) -> list[tuple[int, int]]:
    """Mirror host _stream_pattern_for_bars (emit_immediate_first_step=True)."""
    if pass_key == "overdub":
        overdub_bars = int(config.get("overdub_bars", 0) or 0)
        low_note = int(config.get("overdub_low_note", 24))
        high_note = int(config.get("overdub_high_note", 39))
        step_clocks = OVERDUB_GRID_STEP_CLOCKS
        delay_bars = int(config.get("overdub_start_delay_bars", 0) or 0)
        delay_beats = int(config.get("overdub_start_delay_beats", 1) or 0)
    else:
        overdub_bars = int(config.get("second_overdub_bars", 0) or 0)
        low_note = int(config.get("second_overdub_low_note", 12))
        high_note = int(config.get("second_overdub_high_note", 35))
        step_clocks = int(config.get("second_overdub_step_clocks", 24) or 24)
        delay_bars = int(config.get("second_overdub_start_delay_bars", 0) or 0)
        delay_beats = int(config.get("second_overdub_start_delay_beats", 1) or 0)

    pitch_cycle_bars = int(config.get("pitch_cycle_bars", 2) or 2)
    if overdub_bars <= 0 or high_note < low_note or step_clocks <= 0:
        return []

    note_range = list(range(low_note, high_note + 1))
    target_clocks = overdub_bars * MIDI_CLOCKS_PER_BAR
    phase_delay_clocks = delay_bars * MIDI_CLOCKS_PER_BAR + delay_beats * 24
    pitch_cycle_clocks = pitch_cycle_bars * MIDI_CLOCKS_PER_BAR

    pairs: list[tuple[int, int]] = []
    note_index = 0
    phase_clock_count = 0
    next_step_clock = step_clocks
    immediate_emitted = False

    while phase_clock_count < target_clocks:
        phase_clock_count += 1
        if phase_clock_count <= phase_delay_clocks:
            continue
        if not immediate_emitted:
            note = note_range[note_index % len(note_range)]
            tick = _phase_clock_to_storage_tick(phase_clock_count) % loop_length
            pairs.append((tick, note))
            note_index += 1
            immediate_emitted = True
            continue
        if phase_clock_count >= next_step_clock:
            note = note_range[note_index % len(note_range)]
            tick = _phase_clock_to_storage_tick(phase_clock_count) % loop_length
            pairs.append((tick, note))
            note_index += 1
            if phase_clock_count % pitch_cycle_clocks == 0:
                note_index = 0
            next_step_clock += step_clocks
    return pairs

def materialized_note_pairs_from_base_seed(
    lines: list[str],
    config: Mapping[str, Any],
) -> tuple[int, list[tuple[int, int]]]:
    """Record REVT + both overdub passes from base preset config (REVT omits overdub layers)."""
    record_bars = int(config.get("record_bars", 2) or 2)
    midi_channel = int(config.get("midi_channel", 5) or 5)
    default_length = record_bars * TICKS_PER_BAR

    recs = _extract_last_recs_stop(lines)
    loop_length = recs["final_length"] if recs is not None else default_length
    if loop_length <= 0:
        loop_length = default_length

    seen: set[tuple[int, int]] = set()
    pairs: list[tuple[int, int]] = []

    def add_pair(tick: int, pitch: int) -> None:
        key = (tick % loop_length, pitch)
        if key in seen:
            return
        seen.add(key)
        pairs.append(key)

    for tick, pitch in _extract_revt_notes(lines):
        add_pair(tick, pitch)

    boundaries = _extract_phase_boundaries(lines)
    after_ts = boundaries.get("overdub_start_ts")
    sevt_pairs: list[tuple[int, int]] = []
    for ev in _extract_sevt_events(lines, after_ts=after_ts):
        if not ev.get("is_on") or int(ev["ch"]) != midi_channel:
            continue
        tick = int(ev["tick"]) % loop_length
        pitch = int(ev["note"])
        sevt_pairs.append((tick, pitch))
        add_pair(tick, pitch)

    if not sevt_pairs:
        for pass_key in ("overdub", "second_overdub"):
            for tick, pitch in overdub_pass_pairs_from_base_config(
                config, pass_key=pass_key, loop_length=loop_length
            ):
                add_pair(tick, pitch)

    pairs.sort(key=lambda item: (item[0], item[1]))
    return loop_length, pairs

def nav_slot_stats(note_pairs: list[tuple[int, int]], *, loop_length: int) -> dict[str, int]:
    nav_slots = _build_select_navigation_slots(
        note_pairs, loop_length=loop_length, loop_start=0
    )
    by_step: dict[int, int] = {}
    for tick, _pitch in note_pairs:
        step = (tick % loop_length) // TICKS_PER_16TH_STEP
        by_step[step] = by_step.get(step, 0) + 1
    multi_step = sum(1 for count in by_step.values() if count > 1)
    max_per_step = max(by_step.values()) if by_step else 0
    return {
        "nav_slots": len(nav_slots),
        "note_pairs": len(note_pairs),
        "sixteenth_steps_with_multiple_notes": multi_step,
        "max_notes_per_sixteenth_step": max_per_step,
    }

def record_layout_from_base_seed(
    lines: list[str],
    config: Mapping[str, Any],
) -> RecordLayout:
    loop_length, note_pairs = materialized_note_pairs_from_base_seed(lines, config)
    nav_slots = _build_select_navigation_slots(
        note_pairs, loop_length=loop_length, loop_start=0
    )
    return RecordLayout(
        loop_start=0,
        loop_length=loop_length,
        step_to_tick={},
        nav_slots=nav_slots,
    )

def record_layout_from_edit_fixture(
    config: Mapping[str, Any],
) -> "RecordLayout":
    """Nav layout for EDIT_RECORD_FIXTURE (edit_minimal base seed)."""
    from hitl.control_constants import TICKS_PER_BAR
    from host_midi_automation_edit_baseline import (
        EDIT_RECORD_FIXTURE,
        RecordLayout,
        _build_fixture_step_to_tick,
        _build_select_navigation_slots,
    )

    record_bars = int(config.get("record_bars", 2) or 2)
    loop_length = record_bars * TICKS_PER_BAR
    note_pairs = [(n.step * TICKS_PER_16TH_STEP, n.pitch) for n in EDIT_RECORD_FIXTURE]
    return RecordLayout(
        loop_start=0,
        loop_length=loop_length,
        step_to_tick=_build_fixture_step_to_tick(
            note_pairs,
            EDIT_RECORD_FIXTURE,
            loop_length=loop_length,
        ),
        nav_slots=_build_select_navigation_slots(
            note_pairs,
            loop_length=loop_length,
            loop_start=0,
        ),
    )

def record_layout_for_base_seed(
    lines: list[str],
    config: Mapping[str, Any],
) -> "RecordLayout":
    from host_midi_automation_edit_baseline import (
        EDIT_RECORD_FIXTURE,
        RecordLayout,
        _build_fixture_step_to_tick,
        _build_select_navigation_slots,
    )

    loop_length, note_pairs = materialized_note_pairs_from_base_seed(lines, config)
    nav_slots = _build_select_navigation_slots(
        note_pairs, loop_length=loop_length, loop_start=0
    )
    step_to_tick: dict[int, int] = {}
    if config.get("edit_record_fixture"):
        step_to_tick = _build_fixture_step_to_tick(
            note_pairs,
            EDIT_RECORD_FIXTURE,
            loop_length=loop_length,
        )
    return RecordLayout(
        loop_start=0,
        loop_length=loop_length,
        step_to_tick=step_to_tick,
        nav_slots=nav_slots,
    )
