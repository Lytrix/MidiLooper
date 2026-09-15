"""Capture-only checks for active MIDI-boundary scheduling evidence.

The report deliberately treats ``OVERDUB_STOPPING`` as an independent active
interval.  A direct OVERDUBBING-to-PLAYING transition is retained as legacy
evidence, but cannot satisfy the deferred-stop scenario.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from pathlib import Path
import re
from typing import Optional

from hitl.serial.protocol import parse_cap_micros, parse_track_transition
from hitl.serial_timing import phase_evidence_start_index


ACTIVE_STATES = frozenset({"RECORDING", "PLAYING", "OVERDUBBING", "OVERDUB_STOPPING"})
_MIDI_INPUT_CALL_RE = re.compile(r"\bmidiHandler\.handleMidiInput\s*\(")
# The service call topology is the gate: all input drains belong in main and
# there are exactly nine of them.  Absolute source lines are not an invariant;
# a harmless include or comment otherwise invalidates every device capture.
_EXPECTED_MIDI_INPUT_CALL_PATH = "src/main.cpp"
_EXPECTED_MIDI_INPUT_CALL_COUNT = 9


@dataclass(frozen=True)
class ActiveTransportInterval:
    state: str
    start_ts: int
    stop_ts: Optional[int]


@dataclass(frozen=True)
class OverdubStoppingSegment:
    overdub_start_ts: int
    stopping_start_ts: int
    destination_ts: int
    destination: str


def _transitions(lines: list[str], *, start_index: int) -> list[tuple[int, str, str]]:
    rows: list[tuple[int, str, str]] = []
    for line in lines[start_index:]:
        ts = parse_cap_micros(line)
        transition = parse_track_transition(line)
        if ts is None or transition is None:
            continue
        from_state, to_state = transition
        rows.append((ts, from_state, to_state))
    return rows


def extract_active_transport_intervals(
    lines: list[str], *, after_index: Optional[int] = None
) -> list[ActiveTransportInterval]:
    """Return every explicitly bounded active Track state interval in the run."""
    transitions = _transitions(lines, start_index=phase_evidence_start_index(lines, after_index))
    intervals: list[ActiveTransportInterval] = []
    for index, (start_ts, _from_state, state) in enumerate(transitions):
        if state not in ACTIVE_STATES:
            continue
        stop_ts = transitions[index + 1][0] if index + 1 < len(transitions) else None
        intervals.append(ActiveTransportInterval(state, start_ts, stop_ts))
    return intervals


def extract_overdub_stopping_segments(
    lines: list[str], *, after_index: Optional[int] = None
) -> tuple[list[OverdubStoppingSegment], list[str]]:
    """Pair the required OVERDUBBING -> STOPPING -> destination sequence.

    Diagnostic messages make malformed or legacy direct paths explicit instead
    of silently folding them into a normal overdub window.
    """
    transitions = _transitions(lines, start_index=phase_evidence_start_index(lines, after_index))
    segments: list[OverdubStoppingSegment] = []
    diagnostics: list[str] = []
    overdub_start: Optional[int] = None
    stopping_start: Optional[int] = None
    for ts, from_state, to_state in transitions:
        if from_state == "PLAYING" and to_state == "OVERDUBBING":
            if overdub_start is not None:
                diagnostics.append("overdub_restart_before_stop")
            overdub_start = ts
            stopping_start = None
            continue
        if from_state == "OVERDUBBING" and to_state == "OVERDUB_STOPPING":
            if overdub_start is None:
                diagnostics.append("stopping_without_overdub_start")
                continue
            stopping_start = ts
            continue
        if from_state == "OVERDUBBING" and to_state in {"PLAYING", "STOPPED"}:
            diagnostics.append("legacy_direct_overdub_stop")
            overdub_start = None
            stopping_start = None
            continue
        if from_state == "OVERDUB_STOPPING" and to_state in {"PLAYING", "STOPPED"}:
            if overdub_start is None or stopping_start is None:
                diagnostics.append("stopping_destination_without_complete_entry")
            else:
                segments.append(
                    OverdubStoppingSegment(overdub_start, stopping_start, ts, to_state)
                )
            overdub_start = None
            stopping_start = None
    if stopping_start is not None:
        diagnostics.append("stopping_without_destination")
    elif overdub_start is not None:
        diagnostics.append("overdub_without_stop")
    return segments, diagnostics


def _overflow_timestamps(lines: list[str], *, start_index: int) -> list[int]:
    """Return first-loss timestamps, falling back to legacy notice timestamps."""
    timestamps: list[int] = []
    for line in lines[start_index:]:
        if "#CAP," not in line or ",RING,overflow" not in line:
            continue
        fields = line.split(",")
        # New capture firmware emits #CAP,<notice-us>,RING,overflow,<first-loss-us>.
        # Older fixtures contain only the notification timestamp.
        ts: Optional[int] = None
        if len(fields) >= 5 and fields[2] == "RING" and fields[3] == "overflow":
            try:
                ts = int(fields[4])
            except ValueError:
                ts = None
        if ts is None:
            ts = parse_cap_micros(line)
        if ts is not None:
            timestamps.append(ts)
    return timestamps


def extract_deadline_samples(lines: list[str], *, start_index: int = 0) -> list[dict[str, object]]:
    """Retained late samples only; never infer deadline success from their absence.

    New rows retain device send/due/deadline times. Legacy notice timestamps
    cannot locate the send in a transport interval. Cadence-derived deadlines
    retain firmware's existing tolerance; they are not measured wire edges.
    """
    samples: list[dict[str, object]] = []
    intervals = extract_active_transport_intervals(lines, after_index=start_index)
    for line in lines[start_index:]:
        fields = line.strip().split(",")
        if len(fields) < 7 or fields[2:4] != ["DIAG", "late_event"]:
            continue
        try:
            notice, late, tick = int(fields[1]), int(fields[5]), int(fields[6])
            sample: dict[str, object] = {
                "notice_us": notice, "midi_class": fields[4], "lateness_us": late,
                "tick": tick, "correlation_valid": False, "state": None,
            }
            if len(fields) >= 10:
                send, due, deadline = map(int, fields[7:10])
                valid = all(0 <= value <= 0xFFFFFFFF for value in (send, due, deadline))
                valid = valid and 0 < late < 0x80000000 and ((send - deadline) & 0xFFFFFFFF) == late
                sample.update(send_us=send, due_us=due, deadline_us=deadline,
                              correlation_valid=valid)
                if valid:
                    # Modular device time permits micros() wrap. An unclosed
                    # interval cannot prove membership of a delayed sample.
                    states = {i.state for i in intervals if i.stop_ts is not None
                              and ((send - i.start_ts) & 0xFFFFFFFF)
                              < ((i.stop_ts - i.start_ts) & 0xFFFFFFFF) < 0x80000000}
                    if len(states) == 1:
                        sample["state"] = states.pop()
            samples.append(sample)
        except ValueError:
            samples.append({"correlation_valid": False, "state": None,
                            "error": "malformed_deadline_sample"})
    return samples


def extract_stop_phase_samples(lines: list[str], *, start_index: int = 0) -> list[dict[str, object]]:
    names = ("track", "slot", "phase", "generation", "epoch", "destination", "calls",
             "crossed", "unknown", "start_us", "end_us", "slack_us")
    samples: list[dict[str, object]] = []
    for line in lines[start_index:]:
        fields = line.strip().split(",")
        if len(fields) < 4 or fields[2:4] != ["DIAG", "stop_phase"]:
            continue
        try:
            if len(fields) != 16:
                raise ValueError("field count")
            sample = dict(zip(names, map(int, fields[4:])))
            if any(v < 0 or v > 0xFFFFFFFF for v in sample.values()):
                raise ValueError("range")
            if sample["calls"] == 0 or sample["crossed"] + sample["unknown"] > sample["calls"]:
                raise ValueError("counts")
            sample["max_duration_us"] = (sample["end_us"] - sample["start_us"]) & 0xFFFFFFFF
            sample["valid"] = True
            samples.append(sample)
        except ValueError:
            samples.append({"valid": False, "error": "malformed_stop_phase_sample"})
    return samples


def _before_or_equal_modular(now_us: int, deadline_us: int) -> bool:
    """True when ``now_us`` is not after ``deadline_us`` on a 32-bit micros clock."""
    return ((now_us - deadline_us) & 0xFFFFFFFF) >= 0x80000000 or now_us == deadline_us


def extract_stop_timeline(lines: list[str], *, start_index: int = 0) -> dict[str, object]:
    """Parse the loss-aware, device-clock stop finalization timeline.

    Grain rows carry the last service instant and their next deadline. Service
    and emission rows are queued from timing-critical contexts and printed by
    the main loop, so their CAP notice timestamp is intentionally ignored.
    """
    grain_names = (
        "track", "slot", "phase", "generation", "epoch", "destination", "service_sequence",
        "start_us", "end_us", "service_actual_us", "service_due_us", "service_deadline_us",
        "service_period_us", "slack_us", "finished_phase",
    )
    grains: list[dict[str, object]] = []
    states: list[dict[str, object]] = []
    services: list[dict[str, object]] = []
    emissions: list[dict[str, object]] = []
    drops: list[dict[str, object]] = []
    malformed = 0
    for line in lines[start_index:]:
        fields = line.strip().split(",")
        if len(fields) < 4 or fields[2] != "DIAG":
            continue
        try:
            if fields[3] == "stop_grain":
                if len(fields) != 19:
                    raise ValueError("field count")
                sample = dict(zip(grain_names, map(int, fields[4:])))
                if any(value < 0 or value > 0xFFFFFFFF for value in sample.values()):
                    raise ValueError("range")
                duration = (sample["end_us"] - sample["start_us"]) & 0xFFFFFFFF
                period = sample["service_period_us"]
                due = sample["service_due_us"]
                deadline = sample["service_deadline_us"]
                slack = sample["slack_us"]
                valid = sample["finished_phase"] in (0, 1)
                if period:
                    valid = valid and deadline == ((due + period) & 0xFFFFFFFF)
                    expected_slack = ((deadline - sample["start_us"]) & 0xFFFFFFFF)
                    if not _before_or_equal_modular(sample["start_us"], deadline):
                        expected_slack = 0
                    valid = valid and slack == expected_slack
                else:
                    valid = valid and slack == 0xFFFFFFFF
                sample.update(duration_us=duration, valid=valid)
                grains.append(sample)
            elif fields[3] == "stop_state":
                if len(fields) != 14:
                    raise ValueError("field count")
                track, slot, from_state, to_state, tick, at, sequence, due, deadline, period = map(
                    int, fields[4:]
                )
                valid = 0 <= track < 8 and 0 <= slot < 8 and 0 <= from_state < 8
                valid = valid and 0 <= to_state < 8 and all(
                    0 <= value <= 0xFFFFFFFF
                    for value in (tick, at, sequence, due, deadline, period)
                )
                valid = valid and (period == 0 or deadline == ((due + period) & 0xFFFFFFFF))
                states.append(
                    {
                        "track": track,
                        "slot": slot,
                        "from_state": from_state,
                        "to_state": to_state,
                        "tick": tick,
                        "at_us": at,
                        "sequence": sequence,
                        "due_us": due,
                        "deadline_us": deadline,
                        "period_us": period,
                        "valid": valid,
                    }
                )
            elif fields[3] == "stop_service":
                if len(fields) != 9:
                    raise ValueError("field count")
                sequence, actual, due, deadline, period = map(int, fields[4:])
                valid = 0 <= sequence <= 0xFFFFFFFF and 0 <= actual <= 0xFFFFFFFF
                valid = valid and 0 <= due <= 0xFFFFFFFF and 0 <= deadline <= 0xFFFFFFFF
                valid = valid and 0 <= period <= 0xFFFFFFFF
                valid = valid and (period == 0 or deadline == ((due + period) & 0xFFFFFFFF))
                services.append({"sequence": sequence, "actual_us": actual, "due_us": due,
                                 "deadline_us": deadline, "period_us": period, "valid": valid})
            elif fields[3] == "stop_emit":
                if len(fields) not in (11, 15):
                    raise ValueError("field count")
                sequence = int(fields[4])
                kind = fields[5]
                tick, send, due, deadline, lateness = map(int, fields[6:11])
                valid = kind in {"on", "off"} and all(
                    0 <= value <= 0xFFFFFFFF
                    for value in (sequence, tick, send, due, deadline, lateness)
                )
                expected_lateness = (send - deadline) & 0xFFFFFFFF
                if expected_lateness >= 0x80000000:
                    expected_lateness = 0
                valid = valid and lateness == expected_lateness
                identity = {}
                if len(fields) == 15:
                    track, slot, channel, note = map(int, fields[11:15])
                    valid = valid and 0 <= track < 8 and 0 <= slot < 8
                    valid = valid and 1 <= channel <= 16 and 0 <= note < 128
                    identity = dict(track=track, slot=slot, channel=channel, note=note)
                emissions.append({"sequence": sequence, "kind": kind, "tick": tick,
                                  "send_us": send, "due_us": due, "deadline_us": deadline,
                                  "lateness_us": lateness, "valid": valid, **identity})
            elif fields[3] == "stop_trace_drop":
                if len(fields) != 5:
                    raise ValueError("field count")
                count = int(fields[4])
                if count <= 0 or count > 0xFFFFFFFF:
                    raise ValueError("count")
                drops.append({"count": count})
        except ValueError:
            malformed += 1

    for grain in grains:
        start, end = int(grain["start_us"]), int(grain["end_us"])
        deadline = int(grain["service_deadline_us"])
        if not grain["service_period_us"]:
            grain["deadline_relation"] = "unknown"
        elif not _before_or_equal_modular(start, deadline):
            grain["deadline_relation"] = "already_overdue_at_start"
        elif not _before_or_equal_modular(end, deadline):
            grain["deadline_relation"] = "crossed_during_grain"
        else:
            grain["deadline_relation"] = "within_deadline"
        # An observed callback can occur inside a grain because timer service
        # preempts main-loop work. Overlap alone does not establish blocking.
        following = [service for service in services if service["valid"]
                     and 0 < ((int(service["actual_us"]) - start) & 0xFFFFFFFF) < 0x80000000]
        next_service = min(following,
                           key=lambda service: (int(service["actual_us"]) - start) & 0xFFFFFFFF,
                           default=None)
        grain["next_observed_service_us"] = None if next_service is None else next_service["actual_us"]
        grain["overlaps_next_observed_service"] = None if next_service is None else (
            ((int(next_service["actual_us"]) - start) & 0xFFFFFFFF)
            <= ((end - start) & 0xFFFFFFFF))

    known_sequences = {int(sample["service_sequence"]) for sample in grains}
    known_sequences.update(int(sample["sequence"]) for sample in services)
    uncorrelated_emissions = [sample for sample in emissions
                              if int(sample["sequence"]) not in known_sequences]
    valid = all(bool(sample.get("valid")) for sample in grains + states + services + emissions)
    state_correlation_complete = any(
        sample["valid"] and sample["from_state"] == 6 and sample["to_state"] == 7
        and any(
            terminal["valid"]
            and 0 <= ((terminal["at_us"] - sample["at_us"]) & 0xFFFFFFFF) < 0x80000000
            and terminal["track"] == sample["track"]
            and terminal["slot"] == sample["slot"]
            and terminal["from_state"] == 7
            and terminal["to_state"] in (1, 5)
            for terminal in states[index + 1:]
        )
        for index, sample in enumerate(states)
    )
    return {
        "grains": grains,
        "states": states,
        "services": services,
        "emissions": emissions,
        "drops": drops,
        "malformed": malformed,
        "uncorrelated_emissions": uncorrelated_emissions,
        "state_correlation_complete": state_correlation_complete,
        "correlation_complete": bool(grains) and valid and not drops and not malformed
                                and not uncorrelated_emissions,
    }


def midi_input_call_sites(repo_root: Path) -> list[str]:
    """Return production MIDI-input invocation sites, excluding declarations."""
    hits: list[str] = []
    for path in (repo_root / "src").rglob("*.cpp"):
        rel = path.relative_to(repo_root).as_posix()
        for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if _MIDI_INPUT_CALL_RE.search(line):
                hits.append(f"{rel}:{line_number}")
    return sorted(hits)


def summarize_midi_boundary_timing(
    lines: list[str], *, after_index: Optional[int] = None, repo_root: Optional[Path] = None
) -> dict[str, object]:
    start_index = phase_evidence_start_index(lines, after_index)
    intervals = extract_active_transport_intervals(lines, after_index=start_index)
    overflow_timestamps = _overflow_timestamps(lines, start_index=start_index)
    overflow_intervals: list[dict[str, object]] = []
    for interval in intervals:
        for overflow_ts in overflow_timestamps:
            if overflow_ts < interval.start_ts:
                continue
            if interval.stop_ts is None or overflow_ts < interval.stop_ts:
                overflow_intervals.append({"state": interval.state, "timestamp": overflow_ts})
    stopping_segments, stopping_diagnostics = extract_overdub_stopping_segments(
        lines, after_index=start_index
    )
    call_sites = midi_input_call_sites(repo_root) if repo_root is not None else []
    extra_call_sites = [
        call_site
        for call_site in call_sites
        if not call_site.startswith(f"{_EXPECTED_MIDI_INPUT_CALL_PATH}:")
    ]
    if repo_root is not None and len(call_sites) != _EXPECTED_MIDI_INPUT_CALL_COUNT:
        extra_call_sites.append(
            f"call_count={len(call_sites)} (expected {_EXPECTED_MIDI_INPUT_CALL_COUNT})"
        )
    return {
        "stop_phase_samples": extract_stop_phase_samples(lines, start_index=start_index),
        "stop_timeline": extract_stop_timeline(lines, start_index=start_index),
        "deadline_samples": extract_deadline_samples(lines, start_index=start_index),
        "deadline_safety_proven": False,  # Sparse one-shots cannot prove every deadline.
        "intervals": [asdict(interval) for interval in intervals],
        "interval_counts": {state: sum(i.state == state for i in intervals) for state in sorted(ACTIVE_STATES)},
        # The report is emitted at a later safe USB flush, so it cannot prove
        # which active interval first lost a row.  It does prove that no
        # MIDI-trace parity conclusion is valid for the session.
        "overflow_timestamps": overflow_timestamps,
        "overflow_intervals": overflow_intervals,
        "overflow_free": not overflow_timestamps,
        "overdub_stopping_segments": [asdict(segment) for segment in stopping_segments],
        "overdub_stopping_diagnostics": stopping_diagnostics,
        "midi_input_call_sites": call_sites,
        "extra_midi_input_call_sites": extra_call_sites,
        "call_site_gate_ok": not extra_call_sites,
        "ok": not overflow_timestamps and not extra_call_sites,
    }


def midi_boundary_timing_issues(summary: dict[str, object]) -> list[str]:
    issues: list[str] = []
    if not bool(summary.get("overflow_free")):
        issues.append("capture_ring_overflow")
    if summary.get("overflow_intervals"):
        issues.append("ring_overflow_in_active_interval")
    if not bool(summary.get("call_site_gate_ok")):
        issues.append("unexpected_midi_input_call_site")
    stop_timeline = summary.get("stop_timeline")
    if isinstance(stop_timeline, dict):
        if stop_timeline.get("drops"):
            issues.append("stop_timeline_trace_drop")
        if stop_timeline.get("malformed"):
            issues.append("malformed_stop_timeline")
        if stop_timeline.get("uncorrelated_emissions"):
            issues.append("uncorrelated_stop_emission")
    return issues
