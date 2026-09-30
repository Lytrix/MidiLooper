"""Serial log timing helpers for HITL verification."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

from hitl.capture_transitions import _phase_ts_from_marker_line
from hitl.contract import PhaseAmbiguity
from hitl.control_constants import MIDI_CLOCKS_PER_BAR, MIDI_CLOCKS_PER_BEAT
from hitl.serial.protocol import track_transitions
from hitl.serial_transport import parse_latest_bpm


def phase_start_delay_clocks(*, delay_bars: int = 0, delay_beats: int = 0) -> int:
    """Host grid delay after ST entry before first note (matches stream_pattern_for_bars)."""
    return max(delay_bars, 0) * MIDI_CLOCKS_PER_BAR + max(delay_beats, 0) * MIDI_CLOCKS_PER_BEAT


_PHASE_SESSION_SLOP_US = 100_000


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


def _session_covers_start(start_ts: int, sessions: list[tuple[int, int]]) -> bool:
    for session_start, session_stop in sessions:
        if abs(start_ts - session_start) <= _PHASE_SESSION_SLOP_US:
            return True
        if session_start <= start_ts <= session_stop:
            return True
    return False


def _next_unused_stop(start_ts: int, stops: list[int], used: set[int]) -> Optional[int]:
    for stop_ts in stops:
        if stop_ts > start_ts and stop_ts not in used:
            return stop_ts
    return None


def phase_evidence_start_index(lines: list[str], after_index: Optional[int] = None) -> int:
    """First line that may authorize phase sessions for this run."""
    if after_index is not None:
        return max(after_index, 0)
    for index, line in enumerate(lines):
        if "#HITL," in line:
            return index
    return 0


@dataclass(frozen=True)
class MatchedPhaseSession:
    name: str
    start_ts: Optional[int]
    stop_ts: Optional[int]
    ambiguity: Optional[PhaseAmbiguity] = None
    diagnostic: str = ""


@dataclass(frozen=True)
class PhaseSessionReport:
    after_index: int
    record: Optional[MatchedPhaseSession] = None
    overdub: Optional[MatchedPhaseSession] = None
    second_overdub: Optional[MatchedPhaseSession] = None
    ambiguities: tuple[PhaseAmbiguity, ...] = ()
    diagnostics: tuple[str, ...] = ()

    def boundaries(self) -> dict[str, Optional[int]]:
        return {
            "record_start_ts": None if self.record is None else self.record.start_ts,
            "record_stop_ts": None if self.record is None else self.record.stop_ts,
            "overdub_start_ts": None if self.overdub is None else self.overdub.start_ts,
            "overdub_stop_ts": None if self.overdub is None else self.overdub.stop_ts,
            "second_overdub_start_ts": None
            if self.second_overdub is None
            else self.second_overdub.start_ts,
            "second_overdub_stop_ts": None
            if self.second_overdub is None
            else self.second_overdub.stop_ts,
        }


def _pair_record_sessions(
    lines: list[str], *, start_index: int
) -> tuple[list[tuple[int, int]], Optional[int]]:
    record_sessions: list[tuple[int, int]] = []
    open_record_start: Optional[int] = None
    for ts, (from_state, to_state) in track_transitions(lines[start_index:]):
        if to_state == "RECORDING":
            open_record_start = ts
        elif (
            from_state == "RECORDING"
            and to_state in ("STOPPED_RECORDING", "STOPPED")
            and open_record_start is not None
        ):
            # A record leg that resolved to a full transport stop (`stopRecordingToStopped`, a
            # transport stop during finalization) is still the end of that leg. Pairing only
            # `STOPPED_RECORDING` skipped exactly that leg on device and paired a later stray take
            # instead (`captures/s4f22-step8-e2`: intended take 2304 ticks ending RECORDING->STOPPED,
            # stray take 768 ticks ending RECORDING->STOPPED_RECORDING; the run scored 768/1536).
            record_sessions.append((open_record_start, ts))
            open_record_start = None

    if record_sessions:
        return record_sessions, open_record_start

    human_record_starts: list[int] = []
    human_record_stops: list[int] = []
    for index, line in enumerate(lines):
        if index < start_index:
            continue
        if "Recording started" in line:
            ts = _phase_ts_from_marker_line(lines, index)
            if ts is not None:
                human_record_starts.append(ts)
        if "Recording stopped" in line:
            ts = _phase_ts_from_marker_line(lines, index)
            if ts is not None:
                human_record_stops.append(ts)
    remaining_stops = list(human_record_stops)
    for start_ts in human_record_starts:
        stop_ts = None
        for candidate in remaining_stops:
            if candidate > start_ts:
                stop_ts = candidate
                break
        if stop_ts is not None:
            record_sessions.append((start_ts, stop_ts))
            remaining_stops.remove(stop_ts)
        elif open_record_start is None:
            open_record_start = start_ts
    return record_sessions, open_record_start


def _armed_record_starts(lines: list[str], *, start_index: int) -> set[int]:
    """Timestamps where a record window was entered from ``ARMED`` (the harness's arm gesture).

    A `RECORDING` entered from another state (e.g. `EMPTY->RECORDING`) is not a window the run
    armed, so it must not win over one that was. See :func:`extract_phase_sessions`.
    """
    return {
        ts
        for ts, (from_state, to_state) in track_transitions(lines[start_index:])
        if to_state == "RECORDING" and from_state == "ARMED"
    }


def _record_take_note_counts(
    lines: list[str], sessions: list[tuple[int, int]], *, start_index: int
) -> list[int]:
    """Host note-on rows (``#CAP,<ts>,MI,U,144,...``) inside each record take's window.

    The count identifies the take the host actually streamed into: a transient take left behind by
    the arm/start sequence carries few or no note-ons, while the intended take carries the whole
    stream. See the selection in :func:`extract_phase_sessions`.
    """
    note_ts: list[int] = []
    for line in lines[start_index:]:
        if ",MI,U," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 8:
            continue
        try:
            ts = int(parts[1])
            msg_type = int(parts[4])
        except ValueError:
            continue
        if msg_type == 144:
            note_ts.append(ts)
    counts: list[int] = []
    for start_ts, stop_ts in sessions:
        if stop_ts <= start_ts:
            counts.append(0)
            continue
        counts.append(sum(1 for ts in note_ts if start_ts <= ts < stop_ts))
    return counts


def _pair_overdub_sessions(
    lines: list[str], *, start_index: int
) -> tuple[list[tuple[int, int]], list[int], list[int]]:
    st_overdub_starts: list[int] = []
    st_overdub_stops: list[int] = []
    for ts, (from_state, to_state) in track_transitions(lines[start_index:]):
        if from_state == "PLAYING" and to_state == "OVERDUBBING":
            st_overdub_starts.append(ts)
        elif from_state == "OVERDUBBING" and to_state in (
            "PLAYING",
            "STOPPED",
            # Deferred stop: the interval's last admissible input is when it leaves
            # OVERDUBBING for OVERDUB_STOPPING; the audible completion is the later
            # OVERDUB_STOPPING -> PLAYING hop. Both are legitimate ends of the phase
            # window, and the earliest matching stop is the precise one.
            "OVERDUB_STOPPING",
        ):
            st_overdub_stops.append(ts)
        elif from_state == "OVERDUB_STOPPING" and to_state in ("PLAYING", "STOPPED"):
            st_overdub_stops.append(ts)

    human_overdub_starts: list[int] = []
    human_overdub_stops: list[int] = []
    odub_enter_stops: list[int] = []
    for index, line in enumerate(lines):
        if index < start_index:
            continue
        if "Overdubbing started" in line:
            ts = _phase_ts_from_marker_line(lines, index)
            if ts is not None:
                human_overdub_starts.append(ts)
        if "Overdubbing stopped" in line:
            ts = _phase_ts_from_marker_line(lines, index)
            if ts is not None:
                human_overdub_stops.append(ts)
        # The production overdub stop is a **deferred** job, so it never logs `Overdubbing stopped`:
        # it admits the stop (`Reentry window open slot=`) and completes it when the retiring owner
        # releases the canonical capture (`Reentry released slot=N generation=G`). Those are the ends
        # of the phase window in the shape the device actually emits, and `_next_unused_stop` takes
        # the earliest match, so the admission line is the precise one. Without them a run whose
        # `#CAP,ST,Track,OVERDUBBING,OVERDUB_STOPPING` / `…,OVERDUB_STOPPING,PLAYING` rows were
        # dropped by the capture ring had no overdub stop at all, which surfaced as
        # `missing_phase_boundaries` and then as a firmware contradiction once the transition counts
        # were read correctly (`captures/s4f22-base-device-14`).
        if "Reentry window open slot=" in line or "Reentry released slot=" in line:
            ts = _phase_ts_from_marker_line(lines, index)
            if ts is not None:
                human_overdub_stops.append(ts)
        if "#CAP," in line and ",ODUB,stop,enter" in line:
            ts = _cap_ts(line)
            if ts is not None:
                odub_enter_stops.append(ts)

    overdub_stops = sorted(set(st_overdub_stops + human_overdub_stops + odub_enter_stops))
    overdub_sessions: list[tuple[int, int]] = []
    used_overdub_stops: set[int] = set()
    for start_ts in st_overdub_starts:
        stop_ts = _next_unused_stop(start_ts, overdub_stops, used_overdub_stops)
        if stop_ts is None:
            continue
        overdub_sessions.append((start_ts, stop_ts))
        used_overdub_stops.add(stop_ts)
    for start_ts in human_overdub_starts:
        if _session_covers_start(start_ts, overdub_sessions):
            continue
        stop_ts = _next_unused_stop(start_ts, overdub_stops, used_overdub_stops)
        if stop_ts is None:
            continue
        overdub_sessions.append((start_ts, stop_ts))
        used_overdub_stops.add(stop_ts)
    overdub_sessions.sort(key=lambda session: session[0])

    unmatched_starts = [
        start_ts
        for start_ts in st_overdub_starts + human_overdub_starts
        if not _session_covers_start(start_ts, overdub_sessions)
    ]
    leftover_stops = [stop_ts for stop_ts in overdub_stops if stop_ts not in used_overdub_stops]
    return overdub_sessions, unmatched_starts, leftover_stops


def extract_phase_sessions(
    lines: list[str], *, after_index: Optional[int] = None
) -> PhaseSessionReport:
    start_index = phase_evidence_start_index(lines, after_index)
    record_sessions, open_record_start = _pair_record_sessions(lines, start_index=start_index)
    overdub_sessions, unmatched_od_starts, leftover_od_stops = _pair_overdub_sessions(
        lines, start_index=start_index
    )

    ambiguities: list[PhaseAmbiguity] = []
    diagnostics: list[str] = []
    record: Optional[MatchedPhaseSession] = None
    if record_sessions:
        # The record leg is the take that **holds the streamed notes**, not the first or the last
        # one. The harness's stop-transport / arm / start-transport sequence can leave a transient
        # take beside the intended one, and both orders have been observed on device:
        #   * `captures/s4f22-step8-e2`: intended take **first** (2304 ticks), stray **last** (768);
        #   * `captures/session_20260930_132517.log`: intended take **last** (1536 ticks = exactly
        #     the 2-bar stream), stray **first** (768) -- "first" scored a window with no notes in
        #     it at all (`record on/off 0/0`, `record_first_note_missing`) while the device had
        #     recorded the stream correctly.
        # So order is not the discriminator. Two pieces of evidence are, in this order:
        #   1. the take holding the host's note-on rows (the stream identifies its own take), and
        #   2. the take entered from `ARMED` (a record window the harness armed) rather than from a
        #      mode change such as `EMPTY->RECORDING` -- the distinction
        #      `test_uses_last_complete_recording_window` asks for, and one a bare "first pair"
        #      rule got wrong at HEAD.
        note_counts = _record_take_note_counts(lines, record_sessions, start_index=start_index)
        armed_starts = _armed_record_starts(lines, start_index=start_index)
        best = max(
            range(len(record_sessions)),
            key=lambda index: (
                note_counts[index] > 0,
                record_sessions[index][0] in armed_starts,
                -index,
            ),
        )
        if note_counts[best] <= 0:
            diagnostics.append(
                "record take chosen without host note rows: "
                f"take {best + 1} of {len(record_sessions)} (armed/tie-break), all takes "
                f"{record_sessions}, armed starts {sorted(armed_starts)}"
            )
        elif len(record_sessions) > 1:
            diagnostics.append(
                f"record take {best + 1} of {len(record_sessions)} chosen: it holds "
                f"{note_counts[best]} host note row(s), all takes {note_counts}"
            )
        start_ts, stop_ts = record_sessions[best]
        record_ambiguity = None
        if stop_ts <= start_ts:
            record_ambiguity = PhaseAmbiguity.OUT_OF_ORDER
            ambiguities.append(record_ambiguity)
        record = MatchedPhaseSession("record", start_ts, stop_ts, record_ambiguity)
    elif open_record_start is not None:
        ambiguities.append(PhaseAmbiguity.MISSING_STOP)
        diagnostics.append("record start has no matching stop")
        record = MatchedPhaseSession(
            "record",
            open_record_start,
            None,
            PhaseAmbiguity.MISSING_STOP,
            "record start has no matching stop",
        )

    if unmatched_od_starts:
        ambiguities.append(PhaseAmbiguity.MISSING_STOP)
        diagnostics.append(f"overdub start(s) without stop: {unmatched_od_starts}")
    first_start: Optional[int] = None
    if overdub_sessions:
        first_start = overdub_sessions[0][0]
    elif unmatched_od_starts:
        first_start = min(unmatched_od_starts)
    for stop_ts in leftover_od_stops:
        if first_start is not None and stop_ts < first_start:
            ambiguities.append(PhaseAmbiguity.STALE_STOP)
            diagnostics.append(f"overdub stop {stop_ts} before first start")
        elif first_start is None:
            ambiguities.append(PhaseAmbiguity.MISSING_START)
            diagnostics.append(f"overdub stop {stop_ts} has no matching start")

    def _overdub_session(name: str, pair: tuple[int, int]) -> MatchedPhaseSession:
        start_ts, stop_ts = pair
        ambiguity = PhaseAmbiguity.OUT_OF_ORDER if stop_ts <= start_ts else None
        if ambiguity is not None:
            ambiguities.append(ambiguity)
        return MatchedPhaseSession(name, start_ts, stop_ts, ambiguity)

    overdub = _overdub_session("overdub", overdub_sessions[0]) if overdub_sessions else None
    second = (
        _overdub_session("second_overdub", overdub_sessions[1])
        if len(overdub_sessions) > 1
        else None
    )
    if unmatched_od_starts and overdub is None:
        overdub = MatchedPhaseSession(
            "overdub",
            unmatched_od_starts[0],
            None,
            PhaseAmbiguity.MISSING_STOP,
            "overdub start has no matching stop",
        )

    unique_ambiguities = tuple(dict.fromkeys(ambiguities))
    return PhaseSessionReport(
        after_index=start_index,
        record=record,
        overdub=overdub,
        second_overdub=second,
        ambiguities=unique_ambiguities,
        diagnostics=tuple(diagnostics),
    )


def extract_phase_boundaries(
    lines: list[str], *, after_index: Optional[int] = None
) -> dict[str, Optional[int]]:
    return extract_phase_sessions(lines, after_index=after_index).boundaries()


def _bpm_for_phase_window(
    lines: list[str],
    *,
    phase_start_ts: int,
    phase_stop_ts: int,
    fallback_bpm: float,
) -> tuple[float, str]:
    window: list[str] = []
    for line in lines:
        if "#CAP," not in line or ",BPM," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 2:
            continue
        try:
            ts = int(parts[1])
        except ValueError:
            continue
        if phase_start_ts <= ts < phase_stop_ts:
            window.append(line)
    bpm = parse_latest_bpm(window) if window else None
    if bpm is not None:
        return bpm, "phase_window"
    return fallback_bpm, "fallback_cli"


def extract_first_note_offset(
    lines: list[str],
    *,
    midi_channel_1based: int,
    phase_start_ts: Optional[int],
    phase_stop_ts: Optional[int],
    fallback_bpm: float = 120.0,
    phase_start_delay_clocks: int = 0,
) -> dict[str, object]:
    delay_clocks = max(phase_start_delay_clocks, 0)
    missing = {
        "offset_us": None,
        "offset_ms": None,
        "offset_clocks": None,
        "phase_start_delay_clocks": delay_clocks,
        "capture_latency_clocks": None,
        "bpm_source": None,
        "first_note_ts": None,
    }
    if phase_start_ts is None or phase_stop_ts is None or phase_stop_ts <= phase_start_ts:
        return {**missing, "phase_missing": True}

    first_note_ts: Optional[int] = None
    for line in lines:
        if ",MI,U," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 8:
            continue
        try:
            ts = int(parts[1])
            msg_type = int(parts[4])
            channel = int(parts[5])
            velocity = int(parts[7])
        except ValueError:
            continue
        if ts < phase_start_ts or ts >= phase_stop_ts:
            continue
        if msg_type == 144 and channel == midi_channel_1based and velocity > 0:
            first_note_ts = ts
            break

    if first_note_ts is None:
        return {**missing, "phase_missing": False}

    offset_us = first_note_ts - phase_start_ts
    bpm, bpm_source = _bpm_for_phase_window(
        lines,
        phase_start_ts=phase_start_ts,
        phase_stop_ts=phase_stop_ts,
        fallback_bpm=fallback_bpm,
    )
    us_per_clock = 60_000_000.0 / (bpm * 24.0)
    offset_clocks = int(round(offset_us / us_per_clock)) if us_per_clock > 0 else 0
    capture_latency_clocks = max(0, offset_clocks - delay_clocks)
    return {
        "offset_us": offset_us,
        "offset_ms": offset_us / 1000.0,
        "offset_clocks": offset_clocks,
        "phase_start_delay_clocks": delay_clocks,
        "capture_latency_clocks": capture_latency_clocks,
        "phase_missing": False,
        "bpm_source": bpm_source,
        "first_note_ts": first_note_ts,
    }
