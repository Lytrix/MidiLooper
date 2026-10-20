"""Serial verification for long-loop bounded display window + play/stop follow."""

from __future__ import annotations

import re
from typing import Optional

from hitl.verify.windowed_lane import row_us

TICKS_PER_BAR = 768
BOUNDED_WINDOW_MIN_BARS = 16
DEFAULT_WINDOW_BARS = 16
PLAY_STOP_BUTTON_NOTE = 40

_SNAP_LOG_ALIASES: tuple[str, ...] = (
    "Detailed window centered on playhead",
    "Button press: Play/Stop (long)",
)
_BTN_DURATION_RE = re.compile(r"duration=(\d+)")


def extract_disp_window_rows(lines: list[str]) -> list[dict[str, object]]:
    """Parse #CAP DISP lines that include windowStartTick, windowBars, windowNoteCount."""
    rows: list[dict[str, object]] = []
    for line_index, line in enumerate(lines):
        if ",DISP," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 14:
            continue
        try:
            loop_len = int(parts[5])
            rows.append(
                {
                    "line_index": line_index,
                    "timestamp": int(parts[1]),
                    "slot": int(parts[3]),
                    "state": parts[4],
                    "loop_len": loop_len,
                    "frame_notes": int(parts[8]),
                    "window_start": int(parts[11]),
                    "window_bars": int(parts[12]),
                    "window_note_count": int(parts[13]),
                    "note_session_open": None,
                    "edit_session": None,
                    "source": "disp",
                }
            )
        except ValueError:
            continue
    return rows


_DISP_WINDOW_TRANSITION_TAG = ",DIAG,disp_window,"


def extract_disp_transition_rows(lines: list[str]) -> list[dict[str, object]]:
    """Parse the display window's **transition receipts** (`DIAG,disp_window`) into the DISP-shaped row.

    `SC_DISP` is raised from the paint path and gated on note counters, so a window that keeps painting the *same*
    content raises nothing: the 2026-10-20 ten-run campaign produced a fully green lane run whose drained set held no
    `state=PLAYING` `DISP` row at all. The receipt is raised by the window owner on every **change** of what the display
    holds — track state, edit-session mode, loop length, window start, window bars — so it answers the freeze / snap /
    hold brackets from the same fields with at most one row per transition instead of one per frame.

    Wire form: `#CAP,<us>,DIAG,disp_window,t=<track>,s=<slot>,st=<state>,ed=<0|1>,es=<session>,len=<ticks>,w=<start>,b=<bars>`.
    `frame_notes`/`window_note_count` are reported as `0` on purpose: this family carries interval metadata, not counts,
    and the count-based readiness check (`verify_follow_window_readiness`) keeps reading `DISP` only.
    """
    rows: list[dict[str, object]] = []
    for line_index, line in enumerate(lines):
        if _DISP_WINDOW_TRANSITION_TAG not in line:
            continue
        fields: dict[str, str] = {}
        for token in line.split(","):
            name, sep, value = token.partition("=")
            if sep:
                fields[name.strip()] = value.strip()
        try:
            rows.append(
                {
                    "line_index": line_index,
                    "timestamp": int(line.split(",")[1]),
                    "slot": int(fields["s"]),
                    "state": fields["st"],
                    "loop_len": int(fields["len"]),
                    "frame_notes": 0,
                    "window_start": int(fields["w"]),
                    "window_bars": int(fields["b"]),
                    "window_note_count": 0,
                    "note_session_open": int(fields.get("ed", -1)),
                    "edit_session": int(fields.get("es", -1)),
                    "source": "disp_window",
                }
            )
        except (KeyError, IndexError, ValueError):
            continue
    return rows


def extract_all_window_rows(lines: list[str]) -> list[dict[str, object]]:
    """Both row families, in **device-time order**.

    The families are emitted by different owners (paint path vs window owner), so a drained capture interleaves them in
    delivery order, not production order. Every bracket below is decided on device time, which is the same comparison the
    device makes; ordering the merged list by `(timestamp, line_index)` keeps "the row before" meaning "the earlier one".
    """
    rows = extract_disp_window_rows(lines) + extract_disp_transition_rows(lines)
    return sorted(rows, key=lambda row: (int(row["timestamp"]), int(row["line_index"])))



def _line_index(lines: list[str], needle: str, *, after: Optional[int] = None) -> Optional[int]:
    for i, line in enumerate(lines):
        if after is not None and i <= after:
            continue
        if needle in line:
            return i
    return None


def _line_index_any(
    lines: list[str], needles: tuple[str, ...], *, after: Optional[int] = None
) -> Optional[int]:
    for needle in needles:
        idx = _line_index(lines, needle, after=after)
        if idx is not None:
            return idx
    return None


def _rows_between(rows: list[dict[str, object]], start: int, end: Optional[int]) -> list[dict[str, object]]:
    """Rows whose stream position fell inside a phase. Collected without an early break: the merged row list is in
    device-time order, so a stream-position bracket is a *fallback* and must not depend on the list's ordering."""
    out: list[dict[str, object]] = []
    for row in rows:
        idx = int(row["line_index"])
        if idx <= start:
            continue
        if end is not None and idx >= end:
            continue
        out.append(row)
    return out


def _stamp_us(lines: list[str], index: Optional[int]) -> Optional[int]:
    """The device stamp of the line at `index`, or `None` when it carries none (or does not exist)."""
    if index is None or index < 0 or index >= len(lines):
        return None
    return row_us(lines[index])


def _phase_rows(
    rows: list[dict[str, object]],
    lines: list[str],
    start_idx: Optional[int],
    end_idx: Optional[int],
) -> list[dict[str, object]]:
    """The rows *produced* inside a phase, bracketed by **device time** where the markers are stamped.

    Stream position is not the phase. A row produced while a track plays is retained and delivered only once the
    transport stops (`row_us`), so in a drained capture it lands far below the marker that ends its phase -- the
    freeze row for a NOTE_EDIT that ended at 165.8 s sat at line 17 939 of a 20 505-line log. Time is the
    comparison the device itself makes, and the two stamp families (`#CAP,<us>` and `[<s>.<ms>]`) share the clock.
    Position is kept for the markers that carry no stamp at all (a plain `entered note edit mode` line), and for
    an open end (`end_idx is None`: everything the device produced after the marker).
    """
    if start_idx is None:
        return []
    start_us = _stamp_us(lines, start_idx)
    end_us = _stamp_us(lines, end_idx) if end_idx is not None else None
    if start_us is None or (end_idx is not None and end_us is None):
        return _rows_between(rows, start_idx, end_idx)
    out: list[dict[str, object]] = []
    for row in rows:
        us = int(row["timestamp"])
        if us <= start_us:
            continue
        if end_us is not None and us >= end_us:
            continue
        out.append(row)
    return out


def _rows_up_to(
    rows: list[dict[str, object]],
    lines: list[str],
    index: Optional[int],
) -> list[dict[str, object]]:
    """The rows produced at or before a marker, by device time where the marker is stamped."""
    if index is None:
        return []
    stamp = _stamp_us(lines, index)
    if stamp is None:
        return [row for row in rows if int(row["line_index"]) <= index]
    return [row for row in rows if int(row["timestamp"]) <= stamp]


def _find_play_stop_hold_release(
    lines: list[str], *, after_index: int, min_duration_ms: int
) -> tuple[Optional[int], Optional[int]]:
    """Return (line_index, duration_ms) for a play/stop button release at or after after_index."""
    note_token = f"Note{PLAY_STOP_BUTTON_NOTE}"
    for i, line in enumerate(lines):
        if i <= after_index:
            continue
        if note_token not in line:
            continue
        match = _BTN_DURATION_RE.search(line)
        if match is None:
            continue
        duration_ms = int(match.group(1))
        if duration_ms >= min_duration_ms:
            return i, duration_ms
    return None, None


def verify_follow_window_readiness(
    lines: list[str], *, max_stale_hold_ms: int = 120
) -> dict[str, object]:
    """Fail when a moved long-loop window holds frameNotes >> windowNotes until idle heal.

    session_20260819_232337: after overdub stop, PLAYING frames stayed 619/523, 667/552,
    671/577 for 141–514 ms, then snapped to equal counts after slice_clean.
    """
    issues: list[str] = []
    holds: list[dict[str, object]] = []
    rows = [
        row
        for row in extract_disp_window_rows(lines)
        if int(row["loop_len"]) > BOUNDED_WINDOW_MIN_BARS * TICKS_PER_BAR
        and str(row["state"]) == "PLAYING"
        and int(row["window_start"]) > 0
    ]
    for index, row in enumerate(rows):
        frame_notes = int(row["frame_notes"])
        window_notes = int(row["window_note_count"])
        split = frame_notes - window_notes
        if split < 50:
            continue
        start_us = int(row["timestamp"])
        healed = None
        for later in rows[index + 1 :]:
            later_us = int(later["timestamp"])
            if later_us - start_us > 800_000:
                break
            later_split = int(later["frame_notes"]) - int(later["window_note_count"])
            if later_split <= 5 and int(later["window_start"]) >= int(row["window_start"]):
                healed = later
                break
        if healed is None:
            continue
        hold_ms = (int(healed["timestamp"]) - start_us) / 1000.0
        holds.append(
            {
                "start_us": start_us,
                "hold_ms": hold_ms,
                "frame_notes": frame_notes,
                "window_notes": window_notes,
                "window_start": int(row["window_start"]),
                "healed_frame_notes": int(healed["frame_notes"]),
                "healed_window_notes": int(healed["window_note_count"]),
            }
        )
        if hold_ms >= max_stale_hold_ms:
            issues.append(
                "stale_follow_window_hold "
                f"window_start={row['window_start']} "
                f"frame={frame_notes} window={window_notes} "
                f"hold_ms={hold_ms:.0f}"
            )
    return {
        "ok": not issues,
        "issues": issues,
        "stale_holds": holds,
        "playing_moved_window_rows": len(rows),
    }


def _hold_window_is_clamped(row: dict[str, object]) -> bool:
    """True when the held window start is at its maximum: `loop_len - window_bars x TICKS_PER_BAR`.

    The detailed window centres on the playhead and `resolveDetailedPaintWindow` clamps the start to
    `loopLength - windowLength`. Past `playhead - windowBars/2 == clamp`, the window cannot advance, so a play/stop hold
    has no travel left and "the window did not move" is geometry, not a tracking failure. Measured 2026-10-20: a 32-bar
    take pinned at 12288 and a 24-bar take at 6144 (`play_stop_hold_did_not_track_playhead unique_starts=[6144]`).
    """
    loop_len = int(row.get("loop_len") or 0)
    bars = int(row.get("window_bars") or 0)
    if loop_len <= 0 or bars <= 0:
        return False
    span = bars * TICKS_PER_BAR
    if span >= loop_len:
        return False
    return int(row.get("window_start") or 0) == loop_len - span


def verify_long_loop_display_window(lines: list[str], args: object) -> dict[str, object]:
    issues: list[str] = []
    warnings: list[str] = []
    phase_markers = list(getattr(args, "phase_markers", []) or [])
    bounded_threshold = BOUNDED_WINDOW_MIN_BARS * TICKS_PER_BAR
    record_bars = int(getattr(args, "record_bars", 24) or 24)
    hold_track_ms = int(getattr(args, "hold_track_ms", 4000) or 4000)
    expected_loop_len = record_bars * TICKS_PER_BAR

    rows = extract_all_window_rows(lines)
    long_loop_rows = [row for row in rows if int(row["loop_len"]) > bounded_threshold]

    if not long_loop_rows:
        issues.append("missing_bounded_window_disp_rows")

    enter_idx = _line_index(lines, "entered note edit mode")
    if enter_idx is None:
        enter_idx = _line_index(lines, "Edit session: NOTE_EDIT")
    if enter_idx is None and any("entered note edit mode" in marker for marker in phase_markers):
        enter_idx = 0
    if enter_idx is None:
        issues.append("missing_enter_note_edit")

    snap_idx = _line_index_any(lines, _SNAP_LOG_ALIASES, after=enter_idx)
    snap_from_marker = "phase:long_press_snap" in phase_markers
    if snap_idx is None and not snap_from_marker:
        issues.append("missing_play_stop_long_press_snap_log")

    snap_anchor = snap_idx if snap_idx is not None else enter_idx

    # The snap **gesture** starts at the play/stop button's note-on, three seconds before the long press fires: while the
    # button is held the window follows by design (`shouldAutoFollowDetailedWindow` includes `isPlayStopButtonHeld()`).
    # The freeze bracket therefore ends where the gesture begins, not where the long press is detected; the 2026-10-20
    # rerun made the difference visible (window 4546 held through NOTE_EDIT, 6144 once the button went down).
    gesture_idx = _line_index(lines, "Button Processor: Ch16 Note40 ON", after=enter_idx)

    if enter_idx is not None and snap_anchor is not None:
        freeze_end_idx = gesture_idx if gesture_idx is not None else snap_idx
        if freeze_end_idx is None and snap_from_marker:
            freeze_end_idx = gesture_idx
        freeze_inside = _phase_rows(rows, lines, enter_idx, freeze_end_idx)
        # The entry sample sits **at or before** the enter marker: the window owner's receipt for the mode change is
        # emitted on the device's own clock, and the `Edit session: NOTE_EDIT` acknowledgement the marker follows lands
        # ~10 ms later (the MIDI ack carries a 10 ms delay) — so the entry row can fall on either side of the marker.
        # It is only a **fallback** when nothing landed inside: the last row before the marker can be a bar-band row
        # from up to a bar earlier (2026-10-20 rerun: 3851 at 108.4 s, 4401 at the 109.9 s entry), and comparing it with
        # the entry row would read ordinary playback drift as freeze drift.
        freeze_before = _rows_up_to(rows, lines, enter_idx)
        if not freeze_inside:
            freeze_anchor = freeze_before[-1:]
            if freeze_anchor:
                warnings.append("freeze_window_rows:entry_anchor_only")
        elif freeze_before and freeze_before[-1].get("note_session_open") is not None and (
            freeze_before[-1].get("note_session_open"),
            freeze_before[-1].get("edit_session"),
        ) == (
            freeze_inside[0].get("note_session_open"),
            freeze_inside[0].get("edit_session"),
        ):
            # The row before the marker is the overlay's own entry sample (same display state as the bracket): it is
            # the window as it was when the overlay opened, so a move between it and the next row is drift.
            freeze_anchor = freeze_before[-1:]
        else:
            # A row from the pre-entry state (a bar-band row from earlier playback) is not an entry sample.
            freeze_anchor = []
        freeze_rows = freeze_anchor + freeze_inside
        if len(freeze_rows) < 1 and "phase:freeze_wait_end" in phase_markers:
            freeze_rows = _rows_up_to(long_loop_rows, lines, enter_idx)[-3:]
            if freeze_rows:
                warnings.append("freeze_disp_sparse:marker_fallback")
        if len(freeze_rows) < 1:
            issues.append(f"insufficient_disp_during_note_edit_freeze count={len(freeze_rows)}")
        else:
            freeze_starts = {int(row["window_start"]) for row in freeze_rows}
            if len(freeze_starts) != 1:
                issues.append(
                    f"window_not_frozen_during_note_edit unique_starts={sorted(freeze_starts)}"
                )
            for row in freeze_rows:
                window_bars = int(row["window_bars"])
                if window_bars < 1 or window_bars > DEFAULT_WINDOW_BARS:
                    issues.append(f"window_bars_out_of_range value={window_bars}")
                    break

    if snap_idx is not None and enter_idx is not None:
        # The snap must leave the **frozen** position: during the gesture's hold the window already tracks the playhead,
        # so comparing against the row just before the snap line would measure the hold, not the snap.
        freeze_rows_for_snap = (_rows_up_to(rows, lines, gesture_idx) if gesture_idx is not None
                                else _phase_rows(rows, lines, enter_idx, snap_idx))
        after_snap_rows = _phase_rows(rows, lines, snap_idx, None)
        if freeze_rows_for_snap and after_snap_rows:
            last_freeze_start = int(freeze_rows_for_snap[-1]["window_start"])
            first_after_snap = int(after_snap_rows[0]["window_start"])
            if last_freeze_start == first_after_snap:
                issues.append(
                    "long_press_snap_did_not_move_window "
                    f"start={last_freeze_start}"
                )
    elif snap_from_marker and enter_idx is not None:
        pre_snap = _rows_up_to(long_loop_rows, lines, enter_idx)
        post_snap = _phase_rows(long_loop_rows, lines, enter_idx, None)[-8:]
        if pre_snap and post_snap and len(post_snap) >= 2:
            if int(pre_snap[-1]["window_start"]) == int(post_snap[0]["window_start"]) == int(
                post_snap[-1]["window_start"]
            ):
                issues.append("long_press_snap_did_not_move_window marker_fallback")

    hold_rows: list[dict[str, object]] = []
    hold_verify_mode = "disp_window_rows"
    hold_start_idx = _line_index(lines, "phase:hold_track_start", after=snap_idx)
    hold_end_idx = _line_index(lines, "phase:hold_track_end", after=hold_start_idx)
    if hold_start_idx is not None:
        hold_rows = _phase_rows(rows, lines, hold_start_idx, hold_end_idx)
    elif snap_anchor is not None and "phase:hold_track_start" in phase_markers:
        hold_rows = _phase_rows(long_loop_rows, lines, snap_anchor, None)[-20:]
    elif snap_idx is not None:
        hold_rows = _phase_rows(rows, lines, snap_idx, None)[-40:]

    hold_release_idx: Optional[int] = None
    hold_release_duration_ms: Optional[int] = None

    def find_hold_release() -> bool:
        """The play/stop release witness, searched on demand (sparse rows, or a window that cannot travel)."""
        nonlocal hold_release_idx, hold_release_duration_ms
        if hold_release_idx is not None:
            return True
        if "phase:hold_track_start" not in phase_markers:
            return False
        min_hold_ms = max(int(hold_track_ms * 0.75), 2000)
        anchor = enter_idx if enter_idx is not None else 0
        hold_release_idx, hold_release_duration_ms = _find_play_stop_hold_release(
            lines, after_index=anchor, min_duration_ms=min_hold_ms
        )
        return hold_release_idx is not None

    hold_indirect_ok = find_hold_release() if len(hold_rows) < 3 else False

    if len(hold_rows) < 3:
        if hold_indirect_ok:
            hold_verify_mode = "indirect_button_hold"
            warnings.append(
                "hold_disp_sparse:accepted_play_stop_hold_release "
                f"duration_ms={hold_release_duration_ms}"
            )
        else:
            issues.append(f"insufficient_disp_during_play_stop_hold count={len(hold_rows)}")
    else:
        hold_starts = [int(row["window_start"]) for row in hold_rows]
        if len(set(hold_starts)) < 2:
            if _hold_window_is_clamped(hold_rows[0]) and find_hold_release():
                # The window sits at its maximum start, so the hold could not move it even if tracking were perfect
                # (`resolveDetailedPaintWindow` clamps to `loopLength - windowLength`). The release witness is the only
                # proof available, and a static window that is *not* clamped keeps the original verdict.
                hold_verify_mode = "indirect_button_hold_clamped"
                warnings.append("hold_window_at_clamp:accepted_play_stop_hold_release")
            elif _hold_window_is_clamped(hold_rows[0]):
                issues.append(
                    "play_stop_hold_did_not_track_playhead "
                    f"unique_starts={sorted(set(hold_starts))} window_at_clamp=1"
                )
            elif hold_indirect_ok:
                hold_verify_mode = "indirect_button_hold"
                warnings.append("hold_disp_static:accepted_play_stop_hold_release")
            else:
                issues.append(
                    "play_stop_hold_did_not_track_playhead "
                    f"unique_starts={sorted(set(hold_starts))}"
                )

    loop_len_ok = any(abs(int(row["loop_len"]) - expected_loop_len) <= TICKS_PER_BAR for row in long_loop_rows)
    if long_loop_rows and not loop_len_ok:
        observed = sorted({int(row["loop_len"]) for row in long_loop_rows})
        issues.append(f"unexpected_loop_len expected≈{expected_loop_len} observed={observed}")

    playing_rows = [row for row in long_loop_rows if str(row["state"]) == "PLAYING"]
    if not playing_rows:
        issues.append("missing_playing_bounded_window_disp")

    return {
        "ok": not issues,
        "issues": issues,
        "warnings": warnings,
        "disp_window_row_count": len(rows),
        "long_loop_disp_row_count": len(long_loop_rows),
        "disp_window_transition_row_count": len(
            [row for row in rows if row.get("source") == "disp_window"]
        ),
        "playing_bounded_window_rows": len(playing_rows),
        "enter_note_edit_line": enter_idx,
        "snap_line": snap_idx,
        "hold_start_line": hold_start_idx,
        "hold_disp_rows": len(hold_rows),
        "hold_verify_mode": hold_verify_mode,
        "hold_release_line": hold_release_idx,
        "hold_release_duration_ms": hold_release_duration_ms,
        "expected_loop_len_ticks": expected_loop_len,
    }
