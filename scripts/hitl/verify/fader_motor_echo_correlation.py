"""MO→MI fader motor echo correlation for NOTE_EDIT select sync captures.

#CAP MO vs MI matrix (per fader):

| Tag | Direction | Meaning |
|-----|-----------|---------|
| MO,224,14 | Teensy → DROID | F2 motor pitchbend + trigger outbound |
| MO,176,15,3 | Teensy → DROID | F4 note-value CC outbound |
| MI,H,224,16 | DROID → Teensy | F1 manual pitchbend (user hand on select fader) |
| MI,H,224,14 | DROID → Teensy | F2 position pitchbend (motor output / manual) |
| MI,H,176,15 | DROID → Teensy | F3/F4 CC feedback |
| MI,H,144,13,<note>,127 | DROID → Teensy | Motor command ack (clear=even, set_changed=odd) |

DROID ch13 ack note map (NOTE_EDIT only):
  F1: clear=80 set_changed=81 | F2: 82/83 | F3: 84/85 | F4: 86/87
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

# Fader → (clear_note, set_changed_note) on ch13
_FADER_ACK_NOTES: dict[str, tuple[int, int]] = {
    "f1": (80, 81),
    "f2": (82, 83),
    "f3": (84, 85),
    "f4": (86, 87),
}


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


@dataclass
class MotorEchoResult:
    mo_f2_count: int = 0
    mi_f2_echo_count: int = 0
    mo_mi_f2_pairs: int = 0
    mo_f2_without_echo: int = 0
    mi_f2_echo_without_mo: int = 0
    mo_f4_count: int = 0
    mi_f4_echo_count: int = 0
    mo_mi_f4_pairs: int = 0
    mo_f4_without_echo: int = 0
    mi_f4_echo_without_mo: int = 0
    mo_pitch_without_set_ack: int = 0
    mo_notegate_without_clear_ack: int = 0
    ch13_ack_count: int = 0
    note_changed_count: int = 0
    visible_f2_updates: int = 0
    visible_f4_updates: int = 0
    mo_mi_f2_miss_rate: float = 0.0
    mo_mi_f4_miss_rate: float = 0.0
    ok: bool = True
    details: list[str] = field(default_factory=list)


def _nearest_in_window(
    events: list[tuple[float, object]],
    anchor_t: float,
    window_s: float,
    *,
    used: set[int] | None = None,
) -> int | None:
    """Return index of nearest event within [anchor_t, anchor_t + window_s]."""
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


def _nearest_bidirectional(
    events: list[tuple[float, object]],
    anchor_t: float,
    window_s: float,
    *,
    used: set[int] | None = None,
) -> int | None:
    """Return index of nearest event within ±window_s of anchor_t."""
    best_idx: int | None = None
    best_dt = window_s + 1.0
    for i, (et, _val) in enumerate(events):
        if used is not None and i in used:
            continue
        dt = abs(et - anchor_t)
        if dt <= window_s and dt < best_dt:
            best_dt = dt
            best_idx = i
    return best_idx


def verify_fader_motor_echo_correlation(
    lines: list[str],
    *,
    pair_window_s: float = 1.0,
    ack_window_s: float = 0.011,
    perceptual_window_s: float = 1.0,
) -> dict[str, object]:
    """Pair Teensy MO motor commands with DROID MI echoes and ch13 ack notes."""
    stamped = _wall_times_and_lines(lines)
    result = MotorEchoResult()

    mo_f2_pitch: list[tuple[float, int]] = []
    mo_f2_notegate: list[tuple[float, None]] = []
    mo_f4_cc: list[tuple[float, int]] = []
    mi_f2_echo: list[tuple[float, int]] = []
    mi_f4_echo: list[tuple[float, int]] = []
    ch13_acks: list[tuple[float, int]] = []
    note_changed_times: list[float] = []

    for t, line in stamped:
        if t <= 0:
            continue

        apply_m = re.search(
            r"#DBG select_apply .* apply=1 reason=note_changed", line
        )
        if apply_m:
            note_changed_times.append(t)

        mo_f2_m = re.search(r"#CAP,\d+,MO,224,14,(\d+),(\d+)", line)
        if mo_f2_m:
            mo_f2_pitch.append(
                (t, _decode_pitchbend_wire(int(mo_f2_m.group(1)), int(mo_f2_m.group(2))))
            )

        mo_ng_m = re.search(r"#CAP,\d+,MO,144,14,0,127", line)
        if mo_ng_m:
            mo_f2_notegate.append((t, None))

        mo_f4_m = re.search(r"#CAP,\d+,MO,176,15,3,(\d+)", line)
        if mo_f4_m:
            mo_f4_cc.append((t, int(mo_f4_m.group(1))))

        mi_f2_m = re.search(r"#CAP,\d+,MI,H,224,14,(\d+),(\d+)", line)
        if mi_f2_m:
            mi_f2_echo.append(
                (t, _decode_pitchbend_wire(int(mi_f2_m.group(1)), int(mi_f2_m.group(2))))
            )

        mi_f4_m = re.search(r"#CAP,\d+,MI,H,176,15,3,(\d+)", line)
        if mi_f4_m:
            mi_f4_echo.append((t, int(mi_f4_m.group(1))))

        ch13_m = re.search(r"#CAP,\d+,MI,H,144,13,(\d+),127", line)
        if ch13_m:
            note = int(ch13_m.group(1))
            ch13_acks.append((t, note))
            result.ch13_ack_count += 1

    result.mo_f2_count = len(mo_f2_pitch)
    result.mi_f2_echo_count = len(mi_f2_echo)
    result.mo_f4_count = len(mo_f4_cc)
    result.mi_f4_echo_count = len(mi_f4_echo)
    result.note_changed_count = len(note_changed_times)

    used_mi_f2: set[int] = set()
    for mo_t, _mo_pb in mo_f2_pitch:
        idx = _nearest_in_window(mi_f2_echo, mo_t, pair_window_s, used=used_mi_f2)
        if idx is not None:
            result.mo_mi_f2_pairs += 1
            used_mi_f2.add(idx)
        else:
            result.mo_f2_without_echo += 1

    used_mi_f4: set[int] = set()
    for mo_t, _mo_cc in mo_f4_cc:
        idx = _nearest_in_window(mi_f4_echo, mo_t, pair_window_s, used=used_mi_f4)
        if idx is not None:
            result.mo_mi_f4_pairs += 1
            used_mi_f4.add(idx)
        else:
            result.mo_f4_without_echo += 1

    result.mi_f2_echo_without_mo = result.mi_f2_echo_count - len(used_mi_f2)
    result.mi_f4_echo_without_mo = result.mi_f4_echo_count - len(used_mi_f4)

    if result.mo_f2_count > 0:
        result.mo_mi_f2_miss_rate = result.mo_f2_without_echo / result.mo_f2_count
    if result.mo_f4_count > 0:
        result.mo_mi_f4_miss_rate = result.mo_f4_without_echo / result.mo_f4_count

    # ch13 ack pairing (F2: pitch → set_changed=83, notegate → clear=82)
    if result.ch13_ack_count > 0:
        _clear_note, set_changed_note = _FADER_ACK_NOTES["f2"]
        used_acks: set[int] = set()
        for mo_t, _pb in mo_f2_pitch:
            idx = _nearest_in_window(ch13_acks, mo_t, ack_window_s, used=used_acks)
            if idx is None or ch13_acks[idx][1] != set_changed_note:
                result.mo_pitch_without_set_ack += 1
            else:
                used_acks.add(idx)

        for mo_t, _ in mo_f2_notegate:
            idx = _nearest_in_window(ch13_acks, mo_t, ack_window_s, used=used_acks)
            if idx is None or ch13_acks[idx][1] != _clear_note:
                result.mo_notegate_without_clear_ack += 1
            else:
                used_acks.add(idx)

    # Perceptual visible updates: distinct MI echo values within window after note_changed
    for nc_t in note_changed_times:
        window_end = nc_t + perceptual_window_s
        f2_vals = {pb for mt, pb in mi_f2_echo if nc_t <= mt <= window_end}
        f4_vals = {cc for mt, cc in mi_f4_echo if nc_t <= mt <= window_end}
        if f2_vals:
            result.visible_f2_updates += 1
        if f4_vals:
            result.visible_f4_updates += 1

    if result.note_changed_count > 0:
        perceptual_rate_f2 = result.visible_f2_updates / result.note_changed_count
        perceptual_rate_f4 = result.visible_f4_updates / result.note_changed_count
    else:
        perceptual_rate_f2 = 0.0
        perceptual_rate_f4 = 0.0

    # ok: strict when ch13 acks present; otherwise informational (MI miss rate documented)
    if result.ch13_ack_count > 0 and result.note_changed_count > 0:
        result.ok = (
            result.mo_pitch_without_set_ack == 0
            and result.mo_notegate_without_clear_ack == 0
        )
    else:
        result.ok = True

    result.details = [
        f"mo_f2_count={result.mo_f2_count}",
        f"mi_f2_echo_count={result.mi_f2_echo_count}",
        f"mo_mi_f2_pairs={result.mo_mi_f2_pairs}",
        f"mo_f2_without_echo={result.mo_f2_without_echo}",
        f"mo_mi_f2_miss_rate={result.mo_mi_f2_miss_rate:.3f}",
        f"mo_f4_count={result.mo_f4_count}",
        f"mi_f4_echo_count={result.mi_f4_echo_count}",
        f"mo_mi_f4_pairs={result.mo_mi_f4_pairs}",
        f"mo_f4_without_echo={result.mo_f4_without_echo}",
        f"mo_mi_f4_miss_rate={result.mo_mi_f4_miss_rate:.3f}",
        f"ch13_ack_count={result.ch13_ack_count}",
        f"note_changed_count={result.note_changed_count}",
        f"visible_f2_updates={result.visible_f2_updates}",
        f"visible_f4_updates={result.visible_f4_updates}",
        f"perceptual_update_rate_f2={perceptual_rate_f2:.3f}",
        f"perceptual_update_rate_f4={perceptual_rate_f4:.3f}",
    ]
    if result.ch13_ack_count > 0:
        result.details.append(
            f"mo_pitch_without_set_ack={result.mo_pitch_without_set_ack}"
        )
        result.details.append(
            f"mo_notegate_without_clear_ack={result.mo_notegate_without_clear_ack}"
        )

    return {
        "fader_motor_echo_ok": result.ok,
        "mo_f2_count": result.mo_f2_count,
        "mi_f2_echo_count": result.mi_f2_echo_count,
        "mo_mi_f2_pairs": result.mo_mi_f2_pairs,
        "mo_f2_without_echo": result.mo_f2_without_echo,
        "mi_f2_echo_without_mo": result.mi_f2_echo_without_mo,
        "mo_f4_count": result.mo_f4_count,
        "mi_f4_echo_count": result.mi_f4_echo_count,
        "mo_mi_f4_pairs": result.mo_mi_f4_pairs,
        "mo_f4_without_echo": result.mo_f4_without_echo,
        "mi_f4_echo_without_mo": result.mi_f4_echo_without_mo,
        "mo_pitch_without_set_ack": result.mo_pitch_without_set_ack,
        "mo_notegate_without_clear_ack": result.mo_notegate_without_clear_ack,
        "ch13_ack_count": result.ch13_ack_count,
        "note_changed_count": result.note_changed_count,
        "visible_f2_updates": result.visible_f2_updates,
        "visible_f4_updates": result.visible_f4_updates,
        "perceptual_update_rate_f2": perceptual_rate_f2,
        "perceptual_update_rate_f4": perceptual_rate_f4,
        "mo_mi_f2_miss_rate": result.mo_mi_f2_miss_rate,
        "mo_mi_f4_miss_rate": result.mo_mi_f4_miss_rate,
        "details": result.details,
    }
