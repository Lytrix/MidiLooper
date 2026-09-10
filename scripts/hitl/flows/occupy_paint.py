"""Occupy-paint HITL: overdub source phrase, then occupy on a later overdub.

Record only seeds loop length (pitch 72). First overdub writes the half-bar
phrase. Second overdub occupies that phrase:

- Same-start 56 at the source on-ticks → Hide
- Phrase +1/4 bar on 48/52 → Shorten (last note wraps)
- That +1/4 phrase moved −1/2 bar (−1/4 vs source) → Hide (first note wraps)

GUS undo/redo is orchestrated by the record baseline.
"""

from __future__ import annotations

import time
from typing import Any, Callable, Optional

from hitl.control_constants import (
    CONTROL_CHANNEL_1BASED,
    MIDI_CLOCKS_PER_BAR,
    RECORD_BUTTON_NOTE,
    TICKS_PER_BAR,
    TICKS_PER_BEAT,
)
from hitl.serial_tick_anchor import TICKS_PER_CLOCK

HALF_BAR_TICKS = TICKS_PER_BAR // 2
QUARTER_BAR_TICKS = TICKS_PER_BAR // 4

# Record seeds loop length only. Occupy targets live on overdub passes.
RECORD_SEED_PITCH = 72
RECORD_SEED_PHRASE: tuple[tuple[int, int], ...] = ((0, RECORD_SEED_PITCH),)

# First-overdub source phrase. 48/52: shorten, then hide. 56: same-start hide.
PHRASE_SHORTEN_HIDE: tuple[tuple[int, int], ...] = ((0, 48), (2, 52))
PHRASE_SAME_START: tuple[tuple[int, int], ...] = ((0, 56),)
PHRASE_HIDE_FIRST: tuple[tuple[int, int], ...] = ((0, 56), (2, 60))
SOURCE_PHRASE: tuple[tuple[int, int], ...] = PHRASE_SHORTEN_HIDE + PHRASE_HIDE_FIRST
RECORD_PHRASE = SOURCE_PHRASE

RECORD_LOW_NOTE = RECORD_SEED_PITCH
RECORD_HIGH_NOTE = RECORD_SEED_PITCH
OCCUPY_LOW_NOTE = 48
OCCUPY_HIGH_NOTE = 60
SOURCE_LOW_NOTE = 48
SOURCE_HIGH_NOTE = 60

WRAP_PITCHES: tuple[int, ...] = (48, 52, 56, 60)
SAME_START_PITCH = 56

OCCUPY_LAPS_COUNT = 2
SOURCE_LAPS_COUNT = 1
OCCUPY_LOOP_BARS = 4
OCCUPY_OVERDUB_BARS = OCCUPY_LOOP_BARS * OCCUPY_LAPS_COUNT
SOURCE_OVERDUB_BARS = OCCUPY_LOOP_BARS


def record_midi_edges(*, loop_bars: int = OCCUPY_LOOP_BARS, origin: int = 0) -> list[tuple[int, int, int]]:
    """Record-pass seed. Not an occupy target."""
    return _phrase_edges(
        origin=origin,
        loop_bars=loop_bars,
        lap_index=0,
        phrase=RECORD_SEED_PHRASE,
        shift_ticks=0,
    )


def source_overdub_midi_edges(
    *,
    loop_bars: int = OCCUPY_LOOP_BARS,
    origin: int = 0,
) -> list[tuple[int, int, int]]:
    """First-overdub half-bar phrase. Later occupy passes shorten/hide these."""
    return _phrase_edges(
        origin=origin,
        loop_bars=loop_bars,
        lap_index=0,
        phrase=SOURCE_PHRASE,
        shift_ticks=0,
    )


def occupy_midi_edges(*, loop_bars: int = OCCUPY_LOOP_BARS, origin: int = 0) -> list[tuple[int, int, int]]:
    """Second-overdub occupy: same-start hide, +1/4 shorten, then −1/2 (net −1/4) hide."""
    if loop_bars <= 0:
        raise ValueError("loop_bars must be > 0")
    edges: list[tuple[int, int, int]] = []
    edges.extend(
        _phrase_edges(
            origin=origin,
            loop_bars=loop_bars,
            lap_index=0,
            phrase=PHRASE_SAME_START,
            shift_ticks=0,
        )
    )
    edges.extend(
        _phrase_edges(
            origin=origin,
            loop_bars=loop_bars,
            lap_index=0,
            phrase=PHRASE_SHORTEN_HIDE,
            shift_ticks=QUARTER_BAR_TICKS,
        )
    )
    edges.extend(
        _phrase_edges(
            origin=origin,
            loop_bars=loop_bars,
            lap_index=1,
            phrase=PHRASE_SHORTEN_HIDE,
            shift_ticks=QUARTER_BAR_TICKS - HALF_BAR_TICKS,
        )
    )
    edges.sort(key=lambda edge: (edge[0], edge[2], edge[1]))
    return edges


def _phrase_edges(
    *,
    origin: int,
    loop_bars: int,
    lap_index: int,
    phrase: tuple[tuple[int, int], ...],
    shift_ticks: int,
) -> list[tuple[int, int, int]]:
    if loop_bars <= 0:
        raise ValueError("loop_bars must be > 0")
    loop_ticks = loop_bars * TICKS_PER_BAR
    lap_origin = origin + (lap_index * loop_ticks)
    edges: list[tuple[int, int, int]] = []
    for bar in range(loop_bars):
        bar_tick = lap_origin + (bar * TICKS_PER_BAR)
        for beat, pitch in phrase:
            on_tick = bar_tick + (beat * TICKS_PER_BEAT) + shift_ticks
            off_tick = on_tick + HALF_BAR_TICKS
            edges.append((on_tick, pitch, 100))
            edges.append((off_tick, pitch, 0))
    edges.sort(key=lambda edge: (edge[0], edge[2], edge[1]))
    return edges


def occupy_record_note_count(*, loop_bars: int = OCCUPY_LOOP_BARS) -> int:
    return sum(1 for _tick, _pitch, velocity in record_midi_edges(loop_bars=loop_bars) if velocity > 0)


def occupy_record_notes_min(*, loop_bars: int = OCCUPY_LOOP_BARS) -> int:
    return max(1, occupy_record_note_count(loop_bars=loop_bars) - 1)


def occupy_source_note_count(*, loop_bars: int = OCCUPY_LOOP_BARS) -> int:
    return sum(
        1 for _tick, _pitch, velocity in source_overdub_midi_edges(loop_bars=loop_bars) if velocity > 0
    )


def occupy_source_notes_min(*, loop_bars: int = OCCUPY_LOOP_BARS) -> int:
    return max(1, occupy_source_note_count(loop_bars=loop_bars) - 1)


def occupy_overdub_note_count(*, loop_bars: int = OCCUPY_LOOP_BARS) -> int:
    return sum(1 for _tick, _pitch, velocity in occupy_midi_edges(loop_bars=loop_bars) if velocity > 0)


def occupy_overdub_notes_min(*, loop_bars: int = OCCUPY_LOOP_BARS) -> int:
    return max(1, occupy_overdub_note_count(loop_bars=loop_bars) - 1)


def wrap_note_count(*, loop_bars: int = OCCUPY_LOOP_BARS) -> int:
    """Loop-wrap incoming holds: shorten last note, hide first note."""
    del loop_bars
    return OCCUPY_LAPS_COUNT


def loop_wrap_occupy_edges(
    *,
    loop_bars: int = OCCUPY_LOOP_BARS,
    origin: int = 0,
) -> list[tuple[int, int, int]]:
    """(on_tick, off_tick, pitch) pairs whose Off stores earlier than On in the loop."""
    loop_ticks = loop_bars * TICKS_PER_BAR
    pending: dict[int, list[int]] = {}
    wraps: list[tuple[int, int, int]] = []
    for tick, pitch, velocity in occupy_midi_edges(loop_bars=loop_bars, origin=origin):
        if velocity > 0:
            pending.setdefault(pitch, []).append(tick)
            continue
        ons = pending.get(pitch)
        if not ons:
            continue
        on_tick = ons.pop(0)
        if (off_tick_mod := tick % loop_ticks) < (on_tick % loop_ticks):
            wraps.append((on_tick % loop_ticks, off_tick_mod, pitch))
    return wraps


def _send_note(out_port: Any, *, channel_0: int, note: int, velocity: int) -> None:
    import mido

    kind = "note_on" if velocity > 0 else "note_off"
    out_port.send(mido.Message(kind, channel=channel_0, note=note, velocity=velocity))


def _stream_clocks(loop_bars: int, *, laps: int) -> int:
    return laps * loop_bars * MIDI_CLOCKS_PER_BAR


def stream_occupy_record_bars(
    out_port: Any,
    serial_collector: Any,
    *,
    midi_channel_1based: int,
    loop_bars: int,
    tempo_bpm: float,
    max_seconds_guard: float,
    stream_start_monotonic: Optional[float] = None,
    stop_press_advance_clocks: int = 0,
    stop_press_ms: int = 120,
    abort: Optional[Any] = None,
) -> tuple[int, int, int, dict[str, float]]:
    """Stream the record seed and press record-stop at a full-loop boundary.

    Stop is anchored to ``loop_bars * TICKS_PER_BAR``, not the last MIDI edge.
    The seed's last Off sits at half-bar into the final bar (~2688 on a 4-bar
    loop). Stopping there leaves ``raw % 768 <= 384``, so firmware
    ``quantizeTransportRecordLength`` floors to 3 bars (2304). Occupy recipes
    require the sealed length to be the full loop.
    """
    del serial_collector
    if loop_bars <= 0:
        raise ValueError("loop_bars must be > 0")
    start = stream_start_monotonic if stream_start_monotonic is not None else time.monotonic()
    seconds_per_tick = 60.0 / tempo_bpm / float(TICKS_PER_BEAT)
    return _play_edges_wall_clock(
        out_port,
        channel_0=midi_channel_1based - 1,
        edges=record_midi_edges(loop_bars=loop_bars, origin=0),
        seconds_per_tick=seconds_per_tick,
        max_seconds_guard=max_seconds_guard,
        abort=abort,
        start=start,
        clocks=_stream_clocks(loop_bars, laps=1),
        stop_press_advance_clocks=stop_press_advance_clocks,
        stop_press_ms=stop_press_ms,
        stop_at_tick=loop_bars * TICKS_PER_BAR,
    )


def stream_occupy_source_phrase(
    out_port: Any,
    in_port: Any,
    serial_collector: Any,
    *,
    midi_channel_1based: int,
    loop_bars: int,
    tempo_bpm: float,
    max_seconds_guard: float,
    abort: Optional[Any] = None,
) -> tuple[int, int, int, dict[str, float]]:
    """Stream the unshifted half-bar phrase on the first overdub pass."""
    return _stream_aligned_edges(
        out_port,
        in_port,
        serial_collector,
        midi_channel_1based=midi_channel_1based,
        loop_bars=loop_bars,
        tempo_bpm=tempo_bpm,
        max_seconds_guard=max_seconds_guard,
        abort=abort,
        edges_fn=source_overdub_midi_edges,
        laps=SOURCE_LAPS_COUNT,
    )


def stream_occupy_overdub_laps(
    out_port: Any,
    in_port: Any,
    serial_collector: Any,
    *,
    midi_channel_1based: int,
    loop_bars: int,
    tempo_bpm: float,
    max_seconds_guard: float,
    abort: Optional[Any] = None,
) -> tuple[int, int, int, dict[str, float]]:
    """Stream same-start / +1/4 / −1/2 occupy laps on the second overdub pass."""
    return _stream_aligned_edges(
        out_port,
        in_port,
        serial_collector,
        midi_channel_1based=midi_channel_1based,
        loop_bars=loop_bars,
        tempo_bpm=tempo_bpm,
        max_seconds_guard=max_seconds_guard,
        abort=abort,
        edges_fn=occupy_midi_edges,
        laps=OCCUPY_LAPS_COUNT,
    )


def _stream_aligned_edges(
    out_port: Any,
    in_port: Any,
    serial_collector: Any,
    *,
    midi_channel_1based: int,
    loop_bars: int,
    tempo_bpm: float,
    max_seconds_guard: float,
    abort: Optional[Any],
    edges_fn: Callable[..., list[tuple[int, int, int]]],
    laps: int,
) -> tuple[int, int, int, dict[str, float]]:
    del in_port
    if loop_bars <= 0:
        raise ValueError("loop_bars must be > 0")
    if tempo_bpm <= 0:
        raise ValueError("tempo_bpm must be > 0")

    from hitl.serial_tick_anchor import TransportTickAnchor

    ch = midi_channel_1based - 1
    loop_ticks = loop_bars * TICKS_PER_BAR
    start = time.monotonic()
    seconds_per_tick = 60.0 / tempo_bpm / float(TICKS_PER_BEAT)
    clocks = _stream_clocks(loop_bars, laps=laps)

    if serial_collector is None:
        return _play_edges_wall_clock(
            out_port,
            channel_0=ch,
            edges=edges_fn(loop_bars=loop_bars, origin=0),
            seconds_per_tick=seconds_per_tick,
            max_seconds_guard=max_seconds_guard,
            abort=abort,
            start=start,
            clocks=clocks,
        )

    anchor = TransportTickAnchor.from_collector(serial_collector, tempo_bpm)
    if not _wait_for_loop_boundary(anchor, serial_collector, loop_ticks, abort):
        return 0, 0, clocks, _empty_timing()

    origin = anchor.estimated_tick()
    origin -= origin % loop_ticks
    note_on_count = 0
    for tick, pitch, velocity in edges_fn(loop_bars=loop_bars, origin=origin):
        if abort is not None and abort.check() is not None:
            break
        if time.monotonic() - start > max_seconds_guard:
            break
        if not anchor.sleep_until_tick(tick, serial_collector, abort=abort):
            if velocity > 0:
                _send_note(out_port, channel_0=ch, note=pitch, velocity=0)
            break
        _send_note(out_port, channel_0=ch, note=pitch, velocity=velocity)
        if velocity > 0:
            note_on_count += 1

    return note_on_count, 0, clocks, _empty_timing()


def _play_edges_wall_clock(
    out_port: Any,
    *,
    channel_0: int,
    edges: list[tuple[int, int, int]],
    seconds_per_tick: float,
    max_seconds_guard: float,
    abort: Optional[Any],
    start: float,
    clocks: int,
    stop_press_advance_clocks: int = 0,
    stop_press_ms: int = 120,
    stop_at_tick: Optional[int] = None,
) -> tuple[int, int, int, dict[str, float]]:
    note_on_count = 0
    first_tick = edges[0][0] if edges else 0
    last_tick = edges[-1][0] if edges else 0
    stop_mono: Optional[float] = None
    if stop_press_advance_clocks > 0 or stop_at_tick is not None:
        base_stop_tick = last_tick if stop_at_tick is None else int(stop_at_tick)
        advance_ticks = max(0, int(stop_press_advance_clocks)) * TICKS_PER_CLOCK
        stop_tick = max(first_tick, base_stop_tick - advance_ticks)
        stop_mono = start + ((stop_tick - first_tick) * seconds_per_tick)
    stop_sent = False

    def _maybe_send_record_stop(now_mono: float) -> None:
        nonlocal stop_sent
        if stop_mono is None or stop_sent or now_mono < stop_mono:
            return
        _send_note(
            out_port,
            channel_0=CONTROL_CHANNEL_1BASED - 1,
            note=RECORD_BUTTON_NOTE,
            velocity=127,
        )
        time.sleep(max(stop_press_ms, 1) / 1000.0)
        _send_note(
            out_port,
            channel_0=CONTROL_CHANNEL_1BASED - 1,
            note=RECORD_BUTTON_NOTE,
            velocity=0,
        )
        stop_sent = True

    for tick, pitch, velocity in edges:
        if abort is not None and abort.check() is not None:
            break
        if time.monotonic() - start > max_seconds_guard:
            break
        target_mono = start + ((tick - first_tick) * seconds_per_tick)
        if stop_mono is not None and not stop_sent and stop_mono <= target_mono:
            remaining_stop = stop_mono - time.monotonic()
            if remaining_stop > 0:
                time.sleep(remaining_stop)
            _maybe_send_record_stop(time.monotonic())
        remaining = target_mono - time.monotonic()
        if remaining > 0:
            time.sleep(remaining)
        _maybe_send_record_stop(time.monotonic())
        _send_note(out_port, channel_0=channel_0, note=pitch, velocity=velocity)
        if velocity > 0:
            note_on_count += 1

    if stop_mono is not None and not stop_sent:
        remaining_stop = stop_mono - time.monotonic()
        if remaining_stop > 0:
            time.sleep(remaining_stop)
        _maybe_send_record_stop(time.monotonic())

    timing = _empty_timing()
    if stop_sent:
        timing["stop_press_sent_during_stream"] = 1.0
    return note_on_count, 0, clocks, timing


def _wait_for_loop_boundary(
    anchor: Any,
    collector: Any,
    loop_ticks: int,
    abort: Optional[Any],
) -> bool:
    now = anchor.estimated_tick()
    phase = now % loop_ticks
    if phase <= QUARTER_BAR_TICKS // 4:
        return True
    target = now + (loop_ticks - phase)
    return anchor.sleep_until_tick(target, collector, abort=abort)


def _empty_timing() -> dict[str, float]:
    return {
        "grid_steps_emitted": 0.0,
        "max_abs_grid_jitter_clocks": 0.0,
        "mean_abs_grid_jitter_clocks": 0.0,
        "stop_press_sent_during_stream": 0.0,
    }
