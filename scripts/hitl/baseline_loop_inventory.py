"""Nav inventory for loops seeded by the base HITL preset (record + 2 overdub passes)."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Mapping

from host_midi_automation_baseline import (
    MIDI_CLOCKS_PER_BAR,
    OVERDUB_GRID_STEP_CLOCKS,
    _extract_phase_boundaries,
    _extract_sevt_events,
)
from host_midi_automation_edit_baseline import (
    TICKS_PER_BAR,
    RecordLayout,
    TICKS_PER_16TH_STEP,
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
    if serial_path and Path(serial_path).is_file():
        return Path(serial_path)
    return None


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


def base_report_usable_for_note_edit_sweep(report: Mapping[str, Any] | None) -> bool:
    return base_report_ok(report) or base_report_loop_materialized(report)


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
