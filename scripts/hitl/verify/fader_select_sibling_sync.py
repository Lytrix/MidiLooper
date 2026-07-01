"""Same-bracket sibling note select → F4 motor sync verifier."""

from __future__ import annotations

import re
from dataclasses import dataclass, field


@dataclass
class SiblingSyncResult:
    sibling_select_count: int = 0
    same_bracket_apply_count: int = 0
    motor_sync_misses: int = 0
    f4_mo_misses: int = 0
    f4_only_sync_count: int = 0
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


def _parse_mo_f2_logical(line: str) -> int | None:
    m = re.search(r"#CAP,\d+,MO,224,14,(\d+),(\d+)", line)
    if not m:
        return None
    return (int(m.group(1)) | (int(m.group(2)) << 7)) - 8192


def _parse_mo_f4_cc(line: str) -> int | None:
    m = re.search(r"#CAP,\d+,MO,176,15,3,(\d+)", line)
    if m:
        return int(m.group(1))
    return None


def verify_fader_select_sibling_sync(
    lines: list[str],
    *,
    sync_window_s: float = 0.05,
) -> dict[str, object]:
    """Fail when same-bracket note apply lacks motor sync + F4 MO within sync_window_s."""
    stamped = _wall_times_and_lines(lines)
    result = SiblingSyncResult()

    motor_sync_events: list[tuple[float, int, str]] = []
    mo_f2_events: list[tuple[float, int]] = []
    mo_f4_events: list[tuple[float, int]] = []
    apply_events: list[tuple[float, int, int, int]] = []

    for t, line in stamped:
        sibling_m = re.search(
            r"Select fader: selected note (-?\d+) at tick (\d+) \((\d+)/(\d+) notes at this position\)",
            line,
        )
        if sibling_m:
            total_at_pos = int(sibling_m.group(4))
            if total_at_pos > 1:
                result.sibling_select_count += 1

        apply_m = re.search(
            r"#DBG select_apply bracket_tick=(\d+) note_idx=(-?\d+) slot=(-?\d+) "
            r"prior_slot=(-?\d+) apply=(\d+)",
            line,
        )
        if apply_m and apply_m.group(5) == "1":
            apply_events.append(
                (
                    t,
                    int(apply_m.group(1)),
                    int(apply_m.group(2)),
                    int(apply_m.group(3)),
                )
            )

        motor_m = re.search(
            r"#DBG select_motor_sync sent=(\d+) note_idx=(-?\d+) prior_note_idx=(-?\d+) "
            r"reason=(\S+)",
            line,
        )
        if motor_m:
            sent = int(motor_m.group(1))
            reason = motor_m.group(4)
            motor_sync_events.append((t, sent, reason))
            if sent and reason == "display_pitch_changed_same_bracket":
                result.f4_only_sync_count += 1

        f2_pb = _parse_mo_f2_logical(line)
        if f2_pb is not None and t > 0:
            mo_f2_events.append((t, f2_pb))

        f4_cc = _parse_mo_f4_cc(line)
        if f4_cc is not None and t > 0:
            mo_f4_events.append((t, f4_cc))

    prev_bracket: int | None = None
    prev_note_idx: int | None = None
    prev_f4_cc: int | None = None
    for t, bracket_tick, note_idx, _slot in apply_events:
        if (
            prev_bracket is not None
            and prev_note_idx is not None
            and bracket_tick == prev_bracket
            and note_idx != prev_note_idx
            and note_idx >= 0
            and prev_note_idx >= 0
        ):
            result.same_bracket_apply_count += 1
            window_end = t + sync_window_s
            motor_sent = any(
                mt >= t and mt <= window_end and sent == 1
                for mt, sent, _reason in motor_sync_events
            )
            f4_changed = False
            for mt, cc in mo_f4_events:
                if mt < t or mt > window_end:
                    continue
                if prev_f4_cc is None or cc != prev_f4_cc:
                    f4_changed = True
                    break
            if not motor_sent:
                result.motor_sync_misses += 1
                result.ok = False
                if len(result.details) < 10:
                    result.details.append(
                        f"same-bracket apply note {prev_note_idx}->{note_idx} at "
                        f"bracket {bracket_tick} t={t:.3f}s: no motor sync within "
                        f"{sync_window_s * 1000:.0f}ms"
                    )
            if not f4_changed:
                result.f4_mo_misses += 1
                result.ok = False
                if len(result.details) < 10:
                    result.details.append(
                        f"same-bracket apply note {prev_note_idx}->{note_idx} at "
                        f"bracket {bracket_tick} t={t:.3f}s: no F4 MO change within "
                        f"{sync_window_s * 1000:.0f}ms"
                    )
        prev_bracket = bracket_tick
        prev_note_idx = note_idx
        for mt, cc in reversed(mo_f4_events):
            if mt <= t:
                prev_f4_cc = cc
                break

    result.details = [
        f"sibling_select_count={result.sibling_select_count}",
        f"same_bracket_apply_count={result.same_bracket_apply_count}",
        f"f4_only_sync_count={result.f4_only_sync_count}",
        f"motor_sync_misses={result.motor_sync_misses}",
        f"f4_mo_misses={result.f4_mo_misses}",
        *result.details,
    ]

    return {
        "fader_select_sibling_sync_ok": result.ok,
        "sibling_select_count": result.sibling_select_count,
        "same_bracket_apply_count": result.same_bracket_apply_count,
        "f4_only_sync_count": result.f4_only_sync_count,
        "motor_sync_misses": result.motor_sync_misses,
        "f4_mo_misses": result.f4_mo_misses,
        "details": result.details,
    }
