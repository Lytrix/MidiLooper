"""Outcome gates for NOTE_EDIT fader1 select → motor follow (dwell gap fix)."""

from __future__ import annotations

import re
from dataclasses import dataclass, field


@dataclass
class DwellGapResult:
    dwell_gaps: int = 0
    dwell_gap_details: list[str] = field(default_factory=list)
    dnte_motor_misses: int = 0
    dnte_motor_details: list[str] = field(default_factory=list)
    select_ignored_count: int = 0
    select_accepted_count: int = 0
    apply_zero_count: int = 0
    apply_one_count: int = 0
    motor_sync_sent_count: int = 0
    motor_sync_unchanged_note_count: int = 0
    motor_sync_empty_step_count: int = 0
    ok: bool = False
    details: list[str] = field(default_factory=list)


def _wall_times_and_lines(lines: list[str]) -> list[tuple[float, str]]:
    out: list[tuple[float, str]] = []
    last_t = 0.0
    for line in lines:
        m = re.match(r"\[(\d+\.\d+)\]", line)
        if m:
            last_t = float(m.group(1))
        out.append((last_t, line))
    return out


def _decode_pitchbend_wire(d1: int, d2: int) -> int:
    return (d1 | (d2 << 7)) - 8192


def _parse_mo_f2_logical(line: str) -> int | None:
    m = re.search(r"#CAP,\d+,MO,224,14,(\d+),(\d+)", line)
    if not m:
        return None
    return _decode_pitchbend_wire(int(m.group(1)), int(m.group(2)))


def _parse_mo_f4_cc(line: str) -> int | None:
    m = re.search(r"#CAP,\d+,MO,176,15,3,(\d+)", line)
    if m:
        return int(m.group(1))
    return None


def _parse_mi_f2_logical(line: str) -> int | None:
    m = re.search(r"#CAP,\d+,MI,H,224,14,(\d+),(\d+)", line)
    if not m:
        return None
    return _decode_pitchbend_wire(int(m.group(1)), int(m.group(2)))


def _parse_mi_f4_cc(line: str) -> int | None:
    m = re.search(r"#CAP,\d+,MI,H,176,15,3,(\d+)", line)
    if m:
        return int(m.group(1))
    return None


def verify_fader_select_dwell_gap(
    lines: list[str],
    *,
    pitch_span_threshold: int = 200,
    motor_window_s: float = 0.3,
    dnte_window_s: float = 0.05,
) -> dict[str, object]:
    """Fail when same-slot F1 pitch dwell has no F2 motor change within motor_window_s."""
    stamped = _wall_times_and_lines(lines)
    result = DwellGapResult()

    select_events: list[tuple[float, int, int, int]] = []
    motor_sync_events: list[tuple[float, int, str]] = []
    mo_f2_events: list[tuple[float, int]] = []
    mo_f4_events: list[tuple[float, int]] = []
    mi_f2_events: list[tuple[float, int]] = []
    mi_f4_events: list[tuple[float, int]] = []
    dnte_events: list[tuple[float, int]] = []

    for t, line in stamped:
        slot_m = re.search(r"#DBG select_slot idx=(-?\d+) pitch=(-?\d+) ignored=(\d)", line)
        if slot_m:
            ignored = int(slot_m.group(3))
            if ignored:
                result.select_ignored_count += 1
            else:
                result.select_accepted_count += 1
                select_events.append(
                    (t, int(slot_m.group(1)), int(slot_m.group(2)), ignored)
                )

        apply_m = re.search(
            r"#DBG select_apply .* slot=(-?\d+) prior_slot=(-?\d+) apply=(\d+)", line
        )
        if apply_m:
            if apply_m.group(3) == "0":
                result.apply_zero_count += 1
            else:
                result.apply_one_count += 1

        motor_m = re.search(
            r"#DBG select_motor_sync sent=(\d+) note_idx=(-?\d+) prior_note_idx=(-?\d+) "
            r"reason=(\S+)"
            r"(?: f2_pb=(-?\d+) f4_cc=(-?\d+) prior_f2_pb=(-?\d+) prior_f4_cc=(-?\d+) "
            r"motor_value_changed=(\d+))?",
            line,
        )
        if motor_m:
            sent = int(motor_m.group(1))
            reason = motor_m.group(4)
            motor_sync_events.append((t, sent, reason))
            if sent:
                result.motor_sync_sent_count += 1
            elif reason == "unchanged_note":
                result.motor_sync_unchanged_note_count += 1
            elif reason == "empty_step_ignored":
                result.motor_sync_empty_step_count += 1

        f2_pb = _parse_mo_f2_logical(line)
        if f2_pb is not None and t > 0:
            mo_f2_events.append((t, f2_pb))

        f4_cc = _parse_mo_f4_cc(line)
        if f4_cc is not None and t > 0:
            mo_f4_events.append((t, f4_cc))

        mi_f2_pb = _parse_mi_f2_logical(line)
        if mi_f2_pb is not None and t > 0:
            mi_f2_events.append((t, mi_f2_pb))

        mi_f4_cc = _parse_mi_f4_cc(line)
        if mi_f4_cc is not None and t > 0:
            mi_f4_events.append((t, mi_f4_cc))

        dnte_m = re.search(r"#CAP,\d+,DNTE,\d+,\d+,\d+,\d+,(-?\d+)", line)
        if dnte_m and t > 0:
            dnte_events.append((t, int(dnte_m.group(1))))

    dwell_start: tuple[float, int, int] | None = None
    for t, slot, pitch, _ignored in select_events:
        if dwell_start is None:
            dwell_start = (t, slot, pitch)
            continue
        start_t, start_slot, start_pitch = dwell_start
        if slot != start_slot:
            dwell_start = (t, slot, pitch)
            continue
        span = abs(pitch - start_pitch)
        if span < pitch_span_threshold:
            continue
        window_end = start_t + motor_window_s
        note_sent_in_window = any(
            mt >= start_t and mt <= window_end and sent == 1
            for mt, sent, _reason in motor_sync_events
        )
        unchanged_only_in_window = any(
            mt >= start_t and mt <= window_end and sent == 0 and reason == "unchanged_note"
            for mt, sent, reason in motor_sync_events
        )
        if not note_sent_in_window and unchanged_only_in_window:
            dwell_start = (t, slot, pitch)
            continue
        if mo_f2_events:
            baseline = next((pb for mt, pb in reversed(mo_f2_events) if mt < start_t), None)
            if baseline is None:
                baseline = mo_f2_events[0][1]
            motor_changed = any(
                mt >= start_t and mt <= window_end and pb != baseline
                for mt, pb in mo_f2_events
            )
        else:
            motor_changed = False
        if note_sent_in_window and not motor_changed:
            result.dwell_gaps += 1
            if len(result.dwell_gap_details) < 10:
                result.dwell_gap_details.append(
                    f"slot={slot} t={start_t:.3f}s pitch_span={span} select_motor_sync sent=1 "
                    f"but no MO,224,14 change within {motor_window_s * 1000:.0f}ms"
                )
        elif not note_sent_in_window and not unchanged_only_in_window and not motor_changed:
            result.dwell_gaps += 1
            if len(result.dwell_gap_details) < 10:
                result.dwell_gap_details.append(
                    f"slot={slot} t={start_t:.3f}s pitch_span={span} no MO,224,14 change "
                    f"within {motor_window_s * 1000:.0f}ms (no motor_sync reason logged)"
                )
        dwell_start = (t, slot, pitch)

    for i in range(1, len(dnte_events)):
        prev_t, prev_idx = dnte_events[i - 1]
        cur_t, cur_idx = dnte_events[i]
        if cur_idx == prev_idx:
            continue
        window_start = cur_t - dnte_window_s
        window_end = cur_t + dnte_window_s
        motor_sent = any(
            window_start <= mt <= window_end and sent == 1
            for mt, sent, _reason in motor_sync_events
        )
        f2_hit = any(window_start <= mt <= window_end for mt, _pb in mo_f2_events)
        f4_hit = any(window_start <= mt <= window_end for mt, _cc in mo_f4_events)
        mi_f2_hit = any(window_start <= mt <= window_end for mt, _pb in mi_f2_events)
        mi_f4_hit = any(window_start <= mt <= window_end for mt, _cc in mi_f4_events)
        if not motor_sent and not f2_hit and not f4_hit and not mi_f2_hit and not mi_f4_hit:
            result.dnte_motor_misses += 1
            if len(result.dnte_motor_details) < 10:
                result.dnte_motor_details.append(
                    f"DNTE sel {prev_idx}->{cur_idx} at {cur_t:.3f}s: no F2/F4 MO/MI within "
                    f"±{dnte_window_s * 1000:.0f}ms"
                )

    total_select = result.select_ignored_count + result.select_accepted_count
    ignored_rate = (
        result.select_ignored_count / total_select if total_select else 0.0
    )
    apply_total = result.apply_zero_count + result.apply_one_count
    apply_zero_frac = result.apply_zero_count / apply_total if apply_total else 0.0

    result.ok = result.dwell_gaps == 0 and result.dnte_motor_misses == 0
    result.details = [
        f"select_ignored_rate={ignored_rate:.3f}",
        f"apply_zero_fraction={apply_zero_frac:.3f}",
        f"motor_sync_sent={result.motor_sync_sent_count}",
        f"motor_sync_unchanged_note={result.motor_sync_unchanged_note_count}",
        f"motor_sync_empty_step={result.motor_sync_empty_step_count}",
    ]
    result.details.extend(result.dwell_gap_details[:5])
    result.details.extend(result.dnte_motor_details[:5])

    return {
        "fader_select_dwell_gap_ok": result.ok,
        "dwell_gaps": result.dwell_gaps,
        "dnte_motor_misses": result.dnte_motor_misses,
        "select_ignored_count": result.select_ignored_count,
        "select_accepted_count": result.select_accepted_count,
        "apply_zero_count": result.apply_zero_count,
        "apply_one_count": result.apply_one_count,
        "motor_sync_sent_count": result.motor_sync_sent_count,
        "motor_sync_unchanged_note_count": result.motor_sync_unchanged_note_count,
        "motor_sync_empty_step_count": result.motor_sync_empty_step_count,
        "select_ignored_rate": ignored_rate,
        "apply_zero_fraction": apply_zero_frac,
        "details": result.details,
    }
