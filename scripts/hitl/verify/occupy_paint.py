"""Serial gates for the occupy-paint HITL (OD1 occupy, OD2 adopted cache, GUS)."""

from __future__ import annotations

import re
from typing import Any, Optional

from hitl.control_constants import TICKS_PER_BAR, TICKS_PER_BEAT
from hitl.flows.occupy_paint import HALF_BAR_TICKS, WRAP_PITCHES
from hitl.serial_timing import extract_phase_boundaries

_DRAW_SRC_RE = re.compile(
    r"draw_src,layers,visual,(\d+),preview,(\d+),sealed,(\d+),snap,(\d+),frame,(\d+)"
)
_OVERLAP_HOLD_RE = re.compile(r"DIAG,(?:overlap_hold|oh),.*?,shorten,(\d+),hide,(\d+)")
_VCACHE_ADOPT_RE = re.compile(r"VCACHE,adopt_partial,.*?notes,(\d+),.*?dirty,(\d+)")
_SRC_OPEN_RE = re.compile(r"DIAG,lcr,src,why=open,from=([^,]+)")
_SRCSPAN_VIEW_RE = re.compile(
    r"DIAG,lcr,srcspan,at=view,id=(\d+),p=(\d+),s=(\d+),e=(\d+)"
)


def parse_overlap_hold_rows(lines: list[str]) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for line in lines:
        match = _OVERLAP_HOLD_RE.search(line)
        if match is None:
            continue
        rows.append({"shorten": int(match.group(1)), "hide": int(match.group(2))})
    return rows


def parse_adopt_partial_rows(lines: list[str]) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for line in lines:
        match = _VCACHE_ADOPT_RE.search(line)
        if match is None:
            continue
        rows.append({"notes": int(match.group(1)), "dirty": int(match.group(2))})
    return rows


def parse_wrap_pairs(lines: list[str]) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for line in lines:
        if ",WRAP," not in line or "#CAP," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 7:
            continue
        try:
            rows.append(
                {
                    "on_tick": int(parts[3]),
                    "off_tick": int(parts[4]),
                    "ch": int(parts[5]),
                    "note": int(parts[6]),
                }
            )
        except ValueError:
            continue
    return rows


def parse_loop_length(lines: list[str]) -> Optional[int]:
    """Loop length from sealed DISP rows. Skip RECORDING playhead ticks."""
    latest: Optional[int] = None
    for line in lines:
        if ",DISP," not in line or "#CAP," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 6:
            continue
        state = parts[4].strip()
        if state not in ("PLAYING", "OVERDUBBING"):
            continue
        try:
            length = int(parts[5])
        except ValueError:
            continue
        if length >= TICKS_PER_BAR:
            latest = length
    return latest


def wrap_pair_geometry_ok(on_tick: int, off_tick: int, loop_length: int) -> bool:
    if loop_length < HALF_BAR_TICKS:
        return False
    on_mod = on_tick % loop_length
    off_mod = off_tick % loop_length
    tail_start = loop_length - HALF_BAR_TICKS
    head_end = HALF_BAR_TICKS
    return (
        on_mod >= tail_start
        and on_mod < loop_length
        and off_mod < head_end
        and off_mod < on_mod
    )


def parse_od2_open_source(lines: list[str]) -> Optional[str]:
    for line in lines:
        match = _SRC_OPEN_RE.search(line)
        if match is not None:
            return match.group(1)
    return None


def parse_srcspan_view_rows(lines: list[str]) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for line in lines:
        match = _SRCSPAN_VIEW_RE.search(line)
        if match is None:
            continue
        rows.append(
            {
                "id": int(match.group(1)),
                "pitch": int(match.group(2)),
                "start": int(match.group(3)),
                "end": int(match.group(4)),
            }
        )
    return rows


def parse_draw_src_layers(lines: list[str]) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for line in lines:
        match = _DRAW_SRC_RE.search(line)
        if match is None:
            continue
        rows.append(
            {
                "visual": int(match.group(1)),
                "preview": int(match.group(2)),
                "sealed": int(match.group(3)),
                "snap": int(match.group(4)),
                "frame": int(match.group(5)),
            }
        )
    return rows


def parse_bar_anchors(lines: list[str]) -> list[tuple[int, int]]:
    """(#CAP timestamp, transport tick) from BAR lines."""
    anchors: list[tuple[int, int]] = []
    for line in lines:
        if "#CAP," not in line or ",BAR," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 5:
            continue
        try:
            anchors.append((int(parts[1]), int(parts[3])))
        except ValueError:
            continue
    return anchors


def interpolate_tick_from_bars(
    event_ts: int,
    anchors: list[tuple[int, int]],
    *,
    bpm: float,
) -> Optional[int]:
    if not anchors or bpm <= 0:
        return None
    chosen: Optional[tuple[int, int]] = None
    for ts, tick in anchors:
        if ts <= event_ts:
            chosen = (ts, tick)
        else:
            break
    if chosen is None:
        chosen = anchors[0]
    ticks_per_us = ((bpm / 60.0) * float(TICKS_PER_BEAT)) / 1_000_000.0
    return int(round(chosen[1] + ((event_ts - chosen[0]) * ticks_per_us)))


def parse_mi_u_edges(lines: list[str]) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for line in lines:
        if ",MI,U," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 8:
            continue
        try:
            rows.append(
                {
                    "ts": int(parts[1]),
                    "msg_type": int(parts[4]),
                    "ch": int(parts[5]),
                    "note": int(parts[6]),
                    "velocity": int(parts[7]),
                }
            )
        except ValueError:
            continue
    return rows


def wrap_pairs_from_mi_u(
    lines: list[str],
    *,
    loop_length: int,
    bpm: float = 120.0,
) -> list[dict[str, int]]:
    """Loop-wrap geometry from incoming MI,U when WRAP dump is missing."""
    anchors = parse_bar_anchors(lines)
    pending: dict[int, list[int]] = {}
    wraps: list[dict[str, int]] = []
    for row in parse_mi_u_edges(lines):
        pitch = row["note"]
        if pitch not in WRAP_PITCHES:
            continue
        tick = interpolate_tick_from_bars(row["ts"], anchors, bpm=bpm)
        if tick is None:
            continue
        if row["msg_type"] == 144:
            pending.setdefault(pitch, []).append(tick)
            continue
        if row["msg_type"] != 128:
            continue
        ons = pending.get(pitch)
        if not ons:
            continue
        on_tick = ons.pop(0)
        if wrap_pair_geometry_ok(on_tick, tick, loop_length):
            wraps.append(
                {
                    "on_tick": on_tick % loop_length if loop_length > 0 else on_tick,
                    "off_tick": tick % loop_length if loop_length > 0 else tick,
                    "ch": row["ch"],
                    "note": pitch,
                }
            )
    return wraps


def _cap_ts(line: str) -> Optional[int]:
    if "#CAP," not in line:
        return None
    parts = line.split(",")
    if len(parts) < 2:
        return None
    try:
        return int(parts[1])
    except ValueError:
        return None


def _phase_windows(lines: list[str]) -> dict[str, Optional[int]]:
    """OD1/OD2 windows. Close a missing OD2 ST stop from Overdubbing stopped / ODUB,stop,enter."""
    bounds = extract_phase_boundaries(lines)
    return {
        "od1_start": bounds.get("overdub_start_ts"),
        "od1_stop": bounds.get("overdub_stop_ts"),
        "od2_start": bounds.get("second_overdub_start_ts"),
        "od2_stop": bounds.get("second_overdub_stop_ts"),
    }


def _lines_between(
    lines: list[str],
    start_ts: Optional[int],
    stop_ts: Optional[int],
) -> list[str]:
    if start_ts is None:
        return []
    out: list[str] = []
    for line in lines:
        ts = _cap_ts(line)
        if ts is None:
            continue
        if ts < start_ts:
            continue
        if stop_ts is not None and ts > stop_ts:
            continue
        out.append(line)
    return out


def verify_occupy_paint(lines: list[str], args: object) -> dict[str, Any]:
    issues: list[str] = []
    windows = _phase_windows(lines)
    od1_lines = _lines_between(lines, windows["od1_start"], windows["od1_stop"])
    od2_lines = _lines_between(lines, windows["od2_start"], windows["od2_stop"])
    # Source phrase is OD1. Occupy (same-start / +1/4 / −1/2) is OD2.
    # Seal overlap_hold can land after ST; do not read into the next overdub.
    if windows["od1_stop"] is not None:
        od1_hold_end = windows["od1_stop"] + 5_000_000
        if windows["od2_start"] is not None:
            od1_hold_end = min(od1_hold_end, windows["od2_start"] - 1)
        od1_hold_src = _lines_between(lines, windows["od1_start"], od1_hold_end)
    else:
        od1_hold_src = od1_lines
    source_holds = parse_overlap_hold_rows(od1_hold_src)
    source_shorten = max((row["shorten"] for row in source_holds), default=0)
    source_hide = max((row["hide"] for row in source_holds), default=0)
    if source_shorten > 0:
        issues.append(f"occupy_source_pass_shorten:{source_shorten}")
    if source_hide > 0:
        issues.append(f"occupy_source_pass_hide:{source_hide}")

    if windows["od2_start"] is None:
        occupy_hold_src = []
    elif windows["od2_stop"] is not None:
        occupy_hold_src = _lines_between(lines, windows["od2_start"], windows["od2_stop"] + 5_000_000)
    else:
        occupy_hold_src = od2_lines
    holds = parse_overlap_hold_rows(occupy_hold_src)
    max_shorten = max((row["shorten"] for row in holds), default=0)
    max_hide = max((row["hide"] for row in holds), default=0)
    if max_shorten < 2:
        issues.append(f"occupy_shorten_lt_2:{max_shorten}")
    if max_hide < 2:
        issues.append(f"occupy_hide_lt_2:{max_hide}")

    loop_length = parse_loop_length(lines) or (4 * TICKS_PER_BAR)
    wrap_pairs = [
        row
        for row in parse_wrap_pairs(lines)
        if row["note"] in WRAP_PITCHES
        and wrap_pair_geometry_ok(row["on_tick"], row["off_tick"], loop_length)
    ]
    wrap_source = "WRAP"
    if len(wrap_pairs) < 2:
        wrap_pairs = wrap_pairs_from_mi_u(
            od2_lines if od2_lines else lines,
            loop_length=loop_length,
        )
        wrap_source = "MI_U"
    if len(wrap_pairs) < 2:
        issues.append(f"occupy_wrap_pairs_lt_2:{len(wrap_pairs)}")

    adopts = parse_adopt_partial_rows(lines)
    last_od1_adopt = None
    if windows["od1_stop"] is not None:
        for line in lines:
            ts = _cap_ts(line)
            if ts is None or ts < windows["od1_stop"]:
                continue
            if windows["od2_start"] is not None and ts >= windows["od2_start"]:
                break
            match = _VCACHE_ADOPT_RE.search(line)
            if match is not None:
                last_od1_adopt = {"notes": int(match.group(1)), "dirty": int(match.group(2))}
    if last_od1_adopt is None and adopts:
        last_od1_adopt = adopts[0]
    if last_od1_adopt is None:
        issues.append("occupy_adopt_partial_missing")
    elif last_od1_adopt["notes"] <= 0:
        issues.append("occupy_adopt_partial_empty")

    od2_layers = parse_draw_src_layers(od2_lines)
    first_od2 = od2_layers[0] if od2_layers else None
    od2_open_from = parse_od2_open_source(od2_lines)
    od2_srcspan = parse_srcspan_view_rows(od2_lines)
    if windows["od2_start"] is None:
        issues.append("occupy_second_overdub_missing")
    elif first_od2 is None:
        issues.append("occupy_od2_draw_src_missing")
    else:
        if first_od2["snap"] != 0 and od2_open_from != "snapshot":
            issues.append(f"occupy_od2_snap_not_zero:{first_od2['snap']}")
        if last_od1_adopt is not None and last_od1_adopt["dirty"] == 0:
            adopted = last_od1_adopt["notes"]
            if first_od2["visual"] < adopted:
                issues.append(
                    f"occupy_od2_visual_shrunk:{first_od2['visual']}<{adopted}"
                )

    undo_count = sum(1 for line in lines if "Overdub undone" in line)
    redo_count = sum(1 for line in lines if "Overdub redone" in line)
    expected_cycles = max(1, int(getattr(args, "undo_redo_cycles", 2)))
    if undo_count < expected_cycles:
        issues.append(f"occupy_undo_lt_{expected_cycles}:{undo_count}")
    if redo_count < expected_cycles:
        issues.append(f"occupy_redo_lt_{expected_cycles}:{redo_count}")

    return {
        "ok": len(issues) == 0,
        "issues": issues,
        "overlap_hold": holds,
        "source_overlap_hold": source_holds,
        "source_shorten": source_shorten,
        "source_hide": source_hide,
        "max_shorten": max_shorten,
        "max_hide": max_hide,
        "adopt_partial": last_od1_adopt,
        "od2_first_draw_src": first_od2,
        "od2_open_from": od2_open_from,
        "od2_srcspan": od2_srcspan,
        "undo_count": undo_count,
        "redo_count": redo_count,
        "wrap_pairs": wrap_pairs,
        "wrap_source": wrap_source,
        "windows": windows,
    }
