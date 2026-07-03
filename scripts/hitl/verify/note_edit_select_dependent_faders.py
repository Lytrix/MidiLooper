"""NOTE_EDIT F1 select → F2/F3/F4 motor sync gates (§7.18 / dependent-fader HITL).

Composes existing verifiers and adds outbound value checks against #CAP MO lines.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

from hitl.verify.fader_motor_echo_correlation import verify_fader_motor_echo_correlation
from hitl.verify.note_edit_select_triple_motor_ack import (
    _cluster_note_changed_applies,
    verify_note_edit_select_triple_motor_ack,
)
from hitl.verify.select_motor_timing import (
    MOTOR_MO_WINDOW_S,
    MOTOR_SYNC_MAX_DELAY_S,
    MOTOR_SYNC_MIN_DELAY_S,
    SELECT_CLUSTER_GAP_S,
)


@dataclass
class OutboundValueResult:
    note_changed_count: int = 0
    outbound_ctx_f2_count: int = 0
    outbound_ctx_f4_count: int = 0
    f2_value_misses: int = 0
    f4_value_misses: int = 0
    f2_pb_rel_mismatches: int = 0
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


def _decode_pitchbend_wire(d1: int, d2: int) -> int:
    return (d1 | (d2 << 7)) - 8192


def _parse_mo_f2_pb(line: str) -> int | None:
    m = re.search(r"#CAP,\d+,MO,224,14,(\d+),(\d+)", line)
    if not m:
        return None
    return _decode_pitchbend_wire(int(m.group(1)), int(m.group(2)))


def _parse_mo_f4_cc(line: str) -> int | None:
    m = re.search(r"#CAP,\d+,MO,176,15,3,(\d+)", line)
    if not m:
        return None
    return int(m.group(1))


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


def _nearest_before(
    events: list[tuple[float, object]],
    anchor_t: float,
    lookback_s: float,
) -> int | None:
    best_idx: int | None = None
    best_dt = lookback_s + 1.0
    for i, (et, _val) in enumerate(events):
        dt = anchor_t - et
        if 0 <= dt <= lookback_s and dt < best_dt:
            best_dt = dt
            best_idx = i
    return best_idx


def _nearest_after(
    events: list[tuple[float, object]],
    anchor_t: float,
    lookahead_s: float,
) -> int | None:
    best_idx: int | None = None
    best_dt = lookahead_s + 1.0
    for i, (et, _val) in enumerate(events):
        dt = et - anchor_t
        if 0 <= dt <= lookahead_s and dt < best_dt:
            best_dt = dt
            best_idx = i
    return best_idx


def verify_outbound_motor_values(
    lines: list[str],
    *,
    motor_mo_window_s: float = MOTOR_MO_WINDOW_S,
    motor_sync_min_delay_s: float = MOTOR_SYNC_MIN_DELAY_S,
    motor_sync_max_delay_s: float = MOTOR_SYNC_MAX_DELAY_S,
    cluster_gap_s: float = SELECT_CLUSTER_GAP_S,
    ctx_lookback_s: float = 0.01,
    ctx_lookahead_s: float = 0.1,
) -> dict[str, object]:
    """Each dwell cluster end: MO F2/F4 match outbound_ctx (or select_motor_sync plan)."""
    stamped = _wall_times_and_lines(lines)
    result = OutboundValueResult()

    note_changed_applies: list[tuple[float, int]] = []
    outbound_f2: list[tuple[float, int, int]] = []
    outbound_f4: list[tuple[float, int]] = []
    motor_plan: list[tuple[float, int, int]] = []
    mo_f2: list[tuple[float, int]] = []
    mo_f4: list[tuple[float, int]] = []

    for t, line in stamped:
        if re.search(r"#DBG select_apply .* apply=1 reason=note_changed", line):
            note_m = re.search(r"note_idx=(-?\d+)", line)
            note_idx = int(note_m.group(1)) if note_m else -1
            note_changed_applies.append((t, note_idx))

        f2_ctx = re.search(
            r"#DBG outbound_ctx f2 .* pb=(-?\d+) expected_pb_rel=(-?\d+).*mode=SELECT_SYNC",
            line,
        )
        if f2_ctx:
            result.outbound_ctx_f2_count += 1
            outbound_f2.append(
                (t, int(f2_ctx.group(1)), int(f2_ctx.group(2)))
            )

        f4_ctx = re.search(
            r"#DBG outbound_ctx f4 pitch=(-?\d+) selected_idx=(-?\d+) mode=SELECT_SYNC",
            line,
        )
        if f4_ctx:
            result.outbound_ctx_f4_count += 1
            outbound_f4.append((t, int(f4_ctx.group(1))))

        plan_m = re.search(
            r"#DBG select_motor_sync sent=1 .* f2_pb=(-?\d+) f4_cc=(-?\d+)",
            line,
        )
        if plan_m:
            motor_plan.append((t, int(plan_m.group(1)), int(plan_m.group(2))))

        f2_pb = _parse_mo_f2_pb(line)
        if f2_pb is not None:
            mo_f2.append((t, f2_pb))
        f4_cc = _parse_mo_f4_cc(line)
        if f4_cc is not None:
            mo_f4.append((t, f4_cc))

    result.note_changed_count = len(note_changed_applies)
    used_f2: set[int] = set()
    used_f4: set[int] = set()

    clustered = _cluster_note_changed_applies(
        [(t, note_idx, 0) for t, note_idx in note_changed_applies],
        cluster_gap_s=cluster_gap_s,
    )

    for cluster in clustered:
        apply_t, note_idx, _bracket = cluster[-1]
        expected_f2: int | None = None
        expected_f4: int | None = None

        ctx_f2_idx = _nearest_after(
            [(et, None) for et, _pb, _exp in outbound_f2],
            apply_t,
            ctx_lookahead_s,
        )
        if ctx_f2_idx is None:
            ctx_f2_idx = _nearest_before(
                [(et, None) for et, _pb, _exp in outbound_f2],
                apply_t,
                ctx_lookback_s,
            )
        if ctx_f2_idx is not None:
            expected_f2, expected_pb_rel = outbound_f2[ctx_f2_idx][1], outbound_f2[ctx_f2_idx][2]
            if expected_f2 != expected_pb_rel:
                result.f2_pb_rel_mismatches += 1

        ctx_f4_idx = _nearest_after(
            [(et, None) for et, _pitch in outbound_f4],
            apply_t,
            ctx_lookahead_s,
        )
        if ctx_f4_idx is None:
            ctx_f4_idx = _nearest_before(
                [(et, None) for et, _pitch in outbound_f4],
                apply_t,
                ctx_lookback_s,
            )
        if ctx_f4_idx is not None:
            expected_f4 = outbound_f4[ctx_f4_idx][1]

        plan_idx = _nearest_after(
            [(et, None) for et, _f2, _f4 in motor_plan],
            apply_t,
            motor_sync_max_delay_s,
        )
        if plan_idx is None:
            plan_idx = _nearest_before(
                [(et, None) for et, _f2, _f4 in motor_plan],
                apply_t,
                motor_sync_max_delay_s,
            )
        if plan_idx is not None:
            plan_f2, plan_f4 = motor_plan[plan_idx][1], motor_plan[plan_idx][2]
            if expected_f2 is None:
                expected_f2 = plan_f2
            if expected_f4 is None and plan_f4 >= 0:
                expected_f4 = plan_f4

        mo_anchor_t = apply_t + motor_sync_min_delay_s
        if expected_f2 is not None:
            mo_idx = _first_in_window(mo_f2, mo_anchor_t, motor_mo_window_s, used=used_f2)
            if mo_idx is None:
                result.f2_value_misses += 1
                result.details.append(
                    f"t={apply_t:.3f}s note_idx={note_idx}: no F2 MO within "
                    f"{motor_mo_window_s * 1000:.0f}ms (expected pb={expected_f2})"
                )
            else:
                used_f2.add(mo_idx)
                actual = mo_f2[mo_idx][1]
                if actual != expected_f2:
                    result.f2_value_misses += 1
                    result.details.append(
                        f"t={apply_t:.3f}s note_idx={note_idx}: F2 MO pb={actual} "
                        f"!= expected {expected_f2}"
                    )

        if expected_f4 is not None and note_idx >= 0:
            mo_idx = _first_in_window(mo_f4, mo_anchor_t, motor_mo_window_s, used=used_f4)
            if mo_idx is None:
                result.f4_value_misses += 1
                result.details.append(
                    f"t={apply_t:.3f}s note_idx={note_idx}: no F4 MO within "
                    f"{motor_mo_window_s * 1000:.0f}ms (expected cc={expected_f4})"
                )
            else:
                used_f4.add(mo_idx)
                actual = mo_f4[mo_idx][1]
                if actual != expected_f4:
                    result.f4_value_misses += 1
                    result.details.append(
                        f"t={apply_t:.3f}s note_idx={note_idx}: F4 MO cc={actual} "
                        f"!= expected {expected_f4}"
                    )

    if result.note_changed_count == 0:
        result.details.append("no apply=1 reason=note_changed events in log")

    has_value_expectations = (
        result.outbound_ctx_f2_count > 0
        or result.outbound_ctx_f4_count > 0
        or len(motor_plan) > 0
    )
    result.ok = result.note_changed_count > 0 and (
        not has_value_expectations
        or (result.f2_value_misses == 0 and result.f4_value_misses == 0)
    )

    return {
        "outbound_motor_values_ok": result.ok,
        "note_changed_count": result.note_changed_count,
        "outbound_ctx_f2_count": result.outbound_ctx_f2_count,
        "outbound_ctx_f4_count": result.outbound_ctx_f4_count,
        "f2_value_misses": result.f2_value_misses,
        "f4_value_misses": result.f4_value_misses,
        "f2_pb_rel_mismatches": result.f2_pb_rel_mismatches,
        "details": result.details[:20],
    }


def verify_note_edit_select_dependent_faders(
    lines: list[str],
    args: object | None = None,
    *,
    motor_mo_window_s: float = MOTOR_MO_WINDOW_S,
    min_perceptual_rate_f2: float = 0.80,
    min_perceptual_rate_f4: float = 0.80,
    require_outbound_value_match: bool = True,
) -> dict[str, object]:
    """Full gate bundle for NOTE_EDIT dependent fader motor feedback."""
    triple = verify_note_edit_select_triple_motor_ack(
        lines,
        motor_mo_window_s=motor_mo_window_s,
    )
    echo = verify_fader_motor_echo_correlation(lines)
    values = verify_outbound_motor_values(
        lines,
        motor_mo_window_s=motor_mo_window_s,
    )

    perceptual_f2 = float(echo.get("perceptual_update_rate_f2", 0.0) or 0.0)
    perceptual_f4 = float(echo.get("perceptual_update_rate_f4", 0.0) or 0.0)

    issues: list[str] = []
    if not triple.get("note_edit_select_triple_motor_ack_ok", False):
        issues.append("triple_motor_ack_failed")
        for detail in triple.get("details") or []:
            issues.append(f"triple: {detail}")

    if require_outbound_value_match and not values.get("outbound_motor_values_ok", False):
        issues.append("outbound_motor_values_failed")
        for detail in values.get("details") or []:
            issues.append(f"values: {detail}")

    if perceptual_f2 < min_perceptual_rate_f2:
        issues.append(
            f"perceptual_f2_rate={perceptual_f2:.3f} < min {min_perceptual_rate_f2:.3f}"
        )
    if perceptual_f4 < min_perceptual_rate_f4:
        issues.append(
            f"perceptual_f4_rate={perceptual_f4:.3f} < min {min_perceptual_rate_f4:.3f}"
        )

    pb_rel_mismatches = int(values.get("f2_pb_rel_mismatches", 0) or 0)
    if pb_rel_mismatches > 0:
        issues.append(
            f"f2_pb_rel_mismatches={pb_rel_mismatches} (RC11 anchor vs note-relative tick)"
        )

    ok = len(issues) == 0 and triple.get("note_changed_count", 0) > 0

    return {
        "ok": ok,
        "issues": issues,
        "note_edit_select_triple_motor_ack": triple,
        "fader_motor_echo": echo,
        "outbound_motor_values": values,
        "perceptual_update_rate_f2": perceptual_f2,
        "perceptual_update_rate_f4": perceptual_f4,
        "f2_pb_rel_mismatches": pb_rel_mismatches,
    }


def main() -> int:
    import argparse
    from pathlib import Path

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("serial_log", type=Path)
    parser.add_argument("--min-perceptual-f2", type=float, default=0.0)
    parser.add_argument("--min-perceptual-f4", type=float, default=0.0)
    parser.add_argument(
        "--no-require-outbound-values",
        action="store_true",
        help="Skip MO vs outbound_ctx value pairing (timing-only)",
    )
    ns = parser.parse_args()
    lines = ns.serial_log.read_text(encoding="utf-8", errors="replace").splitlines()
    result = verify_note_edit_select_dependent_faders(
        lines,
        min_perceptual_rate_f2=ns.min_perceptual_f2,
        min_perceptual_rate_f4=ns.min_perceptual_f4,
        require_outbound_value_match=not ns.no_require_outbound_values,
    )
    print(result)
    return 0 if result.get("ok") else 2


if __name__ == "__main__":
    raise SystemExit(main())
