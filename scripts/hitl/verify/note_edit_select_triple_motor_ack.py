"""§7.18.8 acceptance: debounced dwell clusters drive F2+F3+F4 MO + ch13 acks."""

from __future__ import annotations

import re
from dataclasses import dataclass, field

from hitl.verify.select_motor_timing import (
    MOTOR_MO_WINDOW_S,
    MOTOR_SYNC_MAX_DELAY_S,
    MOTOR_SYNC_MIN_DELAY_S,
    NOTEGATE_ACK_WINDOW_S,
    SELECT_CLUSTER_GAP_S,
)

_DEFAULT_MOTOR_MO_WINDOW_S = MOTOR_MO_WINDOW_S
_DEFAULT_MOTOR_SYNC_MIN_DELAY_S = MOTOR_SYNC_MIN_DELAY_S
_DEFAULT_MOTOR_SYNC_MAX_DELAY_S = MOTOR_SYNC_MAX_DELAY_S
_SELECT_FADER_MOTOR_IDLE_S = SELECT_CLUSTER_GAP_S
_DEFAULT_NOTEGATE_ACK_WINDOW_S = NOTEGATE_ACK_WINDOW_S

# DROID ch13 motor-ack notes (NOTE_EDIT): notegate pairs with clear (even), not set_changed.
_CLEAR_ACK = {
    "f2": 82,
    "f3": 84,
    "f4": 86,
}


@dataclass
class TripleMotorAckResult:
    note_changed_count: int = 0
    dwell_cluster_count: int = 0
    select_sync_count: int = 0
    motor_sync_sent_count: int = 0
    live_begin_count: int = 0
    apply_zero_motor_violations: int = 0
    motor_misses: int = 0
    ack_misses: int = 0
    sibling_apply_count: int = 0
    sibling_misses: int = 0
    ok: bool = True
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


def _interpolated_cap_times_and_lines(lines: list[str]) -> list[tuple[float, str]]:
    """Wall-clock anchor + #CAP tick deltas (blocking motor bursts omit DBG lines between MO)."""
    out: list[tuple[float, str]] = []
    last_wall = 0.0
    last_cap_tick: int | None = None
    last_event_t = 0.0
    for line in lines:
        wall_m = re.match(r"\[(\d+\.\d+)\]", line)
        if wall_m:
            last_wall = float(wall_m.group(1))
            last_event_t = last_wall
            last_cap_tick = None
            out.append((last_event_t, line))
            continue
        cap_m = re.match(r"#CAP,(\d+),", line)
        if cap_m:
            cap_tick = int(cap_m.group(1))
            if last_cap_tick is None:
                last_event_t = last_wall
            else:
                last_event_t += (cap_tick - last_cap_tick) / 1_000_000.0
            last_cap_tick = cap_tick
            out.append((last_event_t, line))
            continue
        out.append((last_event_t, line))
    return out


def _first_ack_in_window(
    events: list[tuple[float, int]],
    anchor_t: float,
    window_s: float,
    ack_note: int,
    *,
    used: set[int] | None = None,
) -> int | None:
    best_idx: int | None = None
    best_dt = window_s + 1.0
    for i, (et, note) in enumerate(events):
        if note != ack_note:
            continue
        if used is not None and i in used:
            continue
        dt = et - anchor_t
        if 0 <= dt <= window_s and dt < best_dt:
            best_dt = dt
            best_idx = i
    return best_idx


def _first_in_window(
    events: list[tuple[float, object]],
    anchor_t: float,
    window_s: float,
    *,
    used: set[int] | None = None,
) -> int | None:
    best_idx: int | None = None
    best_dt = window_s + 1.0
    for i, (et, _val) in enumerate(events):
        if used is not None and i in used:
            continue
        dt = et - anchor_t
        if 0 <= dt <= window_s and dt < best_dt:
            best_dt = dt
            best_idx = i
    return best_idx


def _cluster_note_changed_applies(
    applies: list[tuple[float, int, int]],
    *,
    cluster_gap_s: float = _SELECT_FADER_MOTOR_IDLE_S,
) -> list[list[tuple[float, int, int]]]:
    if not applies:
        return []
    clusters: list[list[tuple[float, int, int]]] = []
    current = [applies[0]]
    for item in applies[1:]:
        if item[0] - current[-1][0] < cluster_gap_s:
            current.append(item)
        else:
            clusters.append(current)
            current = [item]
    clusters.append(current)
    return clusters


def _first_in_window_after(
    events: list[tuple[float, object]],
    anchor_t: float,
    min_delay_s: float,
    max_delay_s: float,
    *,
    used: set[int] | None = None,
) -> int | None:
    best_idx: int | None = None
    best_dt = max_delay_s + 1.0
    for i, (et, _val) in enumerate(events):
        if used is not None and i in used:
            continue
        dt = et - anchor_t
        if min_delay_s <= dt <= max_delay_s and dt < best_dt:
            best_dt = dt
            best_idx = i
    return best_idx


def verify_note_edit_select_triple_motor_ack(
    lines: list[str],
    *,
    motor_sync_min_delay_s: float = _DEFAULT_MOTOR_SYNC_MIN_DELAY_S,
    motor_sync_max_delay_s: float = _DEFAULT_MOTOR_SYNC_MAX_DELAY_S,
    motor_mo_window_s: float = _DEFAULT_MOTOR_MO_WINDOW_S,
    ack_window_s: float = _DEFAULT_NOTEGATE_ACK_WINDOW_S,
    cluster_gap_s: float = _SELECT_FADER_MOTOR_IDLE_S,
    max_select_ignored_rate: float = 0.05,
) -> dict[str, object]:
    wall_stamped = _wall_times_and_lines(lines)
    cap_stamped = _interpolated_cap_times_and_lines(lines)
    result = TripleMotorAckResult()

    note_changed_applies: list[tuple[float, int, int]] = []
    apply_zero_times: list[float] = []
    motor_sync_sent_events: list[tuple[float, None]] = []

    mo_f2: list[tuple[float, None]] = []
    mo_f3: list[tuple[float, None]] = []
    mo_f4: list[tuple[float, int]] = []
    mo_f2_trig: list[tuple[float, None]] = []
    mo_f3_trig: list[tuple[float, None]] = []
    mo_f4_trig: list[tuple[float, None]] = []
    ch13_acks: list[tuple[float, int]] = []

    select_ignored = 0
    select_accepted = 0
    first_begin_t: float | None = None
    session_open_done_t: float | None = None

    for t, line in wall_stamped:
        if re.search(r"#DBG select_apply .* apply=1 reason=note_changed", line):
            note_m = re.search(r"note_idx=(-?\d+)", line)
            bracket_m = re.search(r"bracket_tick=(\d+)", line)
            note_idx = int(note_m.group(1)) if note_m else -1
            bracket = int(bracket_m.group(1)) if bracket_m else 0
            note_changed_applies.append((t, note_idx, bracket))

        if re.search(r"#DBG select_apply .* apply=0 reason=unchanged_note", line):
            apply_zero_times.append(t)

        if "mode=SELECT_SYNC" in line:
            result.select_sync_count += 1

        if re.search(r"#DBG outbound_step=BEGIN", line):
            if first_begin_t is None:
                first_begin_t = t

        if re.search(r"#DBG outbound_step=DONE", line) and first_begin_t is not None:
            if session_open_done_t is None and t - first_begin_t < 5.0:
                session_open_done_t = t
            elif session_open_done_t is not None and t > session_open_done_t:
                result.live_begin_count += 1

        motor_m = re.search(
            r"#DBG select_motor_sync sent=(\d+) note_idx=(-?\d+)", line
        )
        if motor_m:
            if int(motor_m.group(1)) == 1:
                motor_sync_sent_events.append((t, None))
                result.motor_sync_sent_count += 1

        slot_m = re.search(r"#DBG select_slot idx=(-?\d+) pitch=(-?\d+) ignored=(\d)", line)
        if slot_m:
            if int(slot_m.group(3)):
                select_ignored += 1
            else:
                select_accepted += 1

    for t, line in cap_stamped:
        if re.search(r"#CAP,\d+,MO,224,14,", line):
            mo_f2.append((t, None))
        if re.search(r"#CAP,\d+,MO,144,14,0,127", line):
            mo_f2_trig.append((t, None))
        if re.search(r"#CAP,\d+,MO,176,15,2,", line):
            mo_f3.append((t, None))
        if re.search(r"#CAP,\d+,MO,144,15,1,127", line):
            mo_f3_trig.append((t, None))
        f4_m = re.search(r"#CAP,\d+,MO,176,15,3,(\d+)", line)
        if f4_m:
            mo_f4.append((t, int(f4_m.group(1))))
        if re.search(r"#CAP,\d+,MO,144,15,2,127", line):
            mo_f4_trig.append((t, None))

        ch13_m = re.search(r"#CAP,\d+,MI,H,144,13,(\d+),127", line)
        if ch13_m:
            ch13_acks.append((t, int(ch13_m.group(1))))

    if session_open_done_t is not None:
        note_changed_applies = [
            (t, note_idx, bracket)
            for t, note_idx, bracket in note_changed_applies
            if t > session_open_done_t
        ]
        apply_zero_times = [t for t in apply_zero_times if t > session_open_done_t]

    result.note_changed_count = len(note_changed_applies)
    dwell_clusters = _cluster_note_changed_applies(
        note_changed_applies, cluster_gap_s=cluster_gap_s
    )
    result.dwell_cluster_count = len(dwell_clusters)

    used_f2: set[int] = set()
    used_f3: set[int] = set()
    used_f4: set[int] = set()
    used_acks: set[int] = set()
    used_sync: set[int] = set()

    for cluster in dwell_clusters:
        apply_t, note_idx, bracket = cluster[-1]
        for pt, _pn, pb in note_changed_applies:
            if pt < apply_t and pb == bracket and _pn != note_idx:
                result.sibling_apply_count += 1
                break

    for cluster in dwell_clusters:
        apply_t, note_idx, bracket = cluster[-1]
        sync_idx = _first_in_window_after(
            motor_sync_sent_events,
            apply_t,
            motor_sync_min_delay_s,
            motor_sync_max_delay_s,
            used=used_sync,
        )
        if sync_idx is None:
            result.motor_misses += 1
            result.details.append(
                f"t={apply_t:.3f}s note_idx={note_idx}: no select_motor_sync sent=1 between "
                f"{motor_sync_min_delay_s * 1000:.0f}ms and "
                f"{motor_sync_max_delay_s * 1000:.0f}ms after cluster end"
            )
            continue
        used_sync.add(sync_idx)
        sync_t = motor_sync_sent_events[sync_idx][0]

        f2_idx = _first_in_window(mo_f2, sync_t, motor_mo_window_s, used=used_f2)
        f3_idx = _first_in_window(mo_f3, sync_t, motor_mo_window_s, used=used_f3)
        f4_idx = _first_in_window(mo_f4, sync_t, motor_mo_window_s, used=used_f4)

        if f2_idx is None or f3_idx is None or f4_idx is None:
            result.motor_misses += 1
            missing = []
            if f2_idx is None:
                missing.append("F2")
            if f3_idx is None:
                missing.append("F3")
            if f4_idx is None:
                missing.append("F4")
            result.details.append(
                f"t={apply_t:.3f}s note_idx={note_idx} bracket={bracket}: missing MO "
                f"{','.join(missing)} within {motor_mo_window_s * 1000:.0f}ms of motor sync"
            )
        else:
            used_f2.add(f2_idx)
            used_f3.add(f3_idx)
            used_f4.add(f4_idx)

            for label, trig_events, ack_note in (
                ("f2", mo_f2_trig, _CLEAR_ACK["f2"]),
                ("f3", mo_f3_trig, _CLEAR_ACK["f3"]),
                ("f4", mo_f4_trig, _CLEAR_ACK["f4"]),
            ):
                trig_idx = _first_in_window(trig_events, sync_t, motor_mo_window_s, used=None)
                if trig_idx is None:
                    result.ack_misses += 1
                    result.details.append(
                        f"t={apply_t:.3f}s {label.upper()}: no notegate MO within "
                        f"{motor_mo_window_s * 1000:.0f}ms of motor sync"
                    )
                    continue
                mo_t = trig_events[trig_idx][0]
                ack_idx = _first_ack_in_window(
                    ch13_acks, mo_t, ack_window_s, ack_note, used=used_acks
                )
                if ack_idx is None:
                    result.ack_misses += 1
                    result.details.append(
                        f"t={apply_t:.3f}s {label.upper()}: no ch13 ack {ack_note} within "
                        f"{ack_window_s * 1000:.0f}ms of MO"
                    )
                else:
                    used_acks.add(ack_idx)

    for zero_t in apply_zero_times:
        sync_idx = _first_in_window_after(
            motor_sync_sent_events,
            zero_t,
            0.0,
            motor_sync_max_delay_s,
        )
        if sync_idx is not None:
            result.apply_zero_motor_violations += 1

    ignored_rate = 0.0
    total_select = select_ignored + select_accepted
    if total_select > 0:
        ignored_rate = select_ignored / total_select

    if result.note_changed_count > 0:
        if result.select_sync_count < result.dwell_cluster_count:
            result.details.append(
                f"SELECT_SYNC count {result.select_sync_count} < dwell_clusters "
                f"{result.dwell_cluster_count}"
            )
        if result.live_begin_count > 0:
            result.details.append(
                f"live outbound_step=BEGIN count={result.live_begin_count} (expected 0 after session open)"
            )
        if ignored_rate > max_select_ignored_rate:
            result.details.append(
                f"select_ignored_rate={ignored_rate:.3f} > {max_select_ignored_rate}"
            )
        if result.apply_zero_motor_violations > 0:
            result.details.append(
                f"apply_zero_motor_violations={result.apply_zero_motor_violations}"
            )
        if result.motor_misses > 0 or result.ack_misses > 0:
            pass  # already in details
    else:
        result.details.append("no apply=1 reason=note_changed events in log")

    result.ok = (
        result.note_changed_count > 0
        and result.dwell_cluster_count > 0
        and result.select_sync_count >= result.dwell_cluster_count
        and result.motor_sync_sent_count >= result.dwell_cluster_count
        and result.live_begin_count == 0
        and result.motor_misses == 0
        and result.ack_misses == 0
        and result.apply_zero_motor_violations == 0
        and ignored_rate <= max_select_ignored_rate
    )

    return {
        "note_edit_select_triple_motor_ack_ok": result.ok,
        "note_changed_count": result.note_changed_count,
        "dwell_cluster_count": result.dwell_cluster_count,
        "select_sync_count": result.select_sync_count,
        "motor_sync_sent_count": result.motor_sync_sent_count,
        "live_begin_count": result.live_begin_count,
        "motor_misses": result.motor_misses,
        "ack_misses": result.ack_misses,
        "apply_zero_motor_violations": result.apply_zero_motor_violations,
        "select_ignored_rate": ignored_rate,
        "sibling_apply_count": result.sibling_apply_count,
        "details": result.details[:20],
    }


def main() -> int:
    import argparse
    import sys
    from pathlib import Path

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    if not args.log.is_file():
        print(f"Not found: {args.log}", file=sys.stderr)
        return 1
    lines = args.log.read_text(errors="replace").splitlines()
    out = verify_note_edit_select_triple_motor_ack(lines)
    print(out)
    return 0 if out.get("note_edit_select_triple_motor_ack_ok") else 5


if __name__ == "__main__":
    raise SystemExit(main())
