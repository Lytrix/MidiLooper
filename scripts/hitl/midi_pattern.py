"""MIDI-file note patterns for HITL runs (parser option, DEC-221).

The HITL recipes so far generate their notes from a fixture (pitch range, step size, gate). A
musical run needs the notes that were actually played, so this module turns a standard MIDI file
into a **loopable clock pattern** the run can stream against the device's clock grid.

Contract
--------
* A pattern is one loop: any event past the loop length wraps (modulo), so a note-off written at
  the very end of the bar re-triggers cleanly on the next loop instead of sustaining forever.
* Positions are converted to **MIDI clocks** (24 PPQN) because that is the grid the device follows
  when the host is the clock master. Source ticks are kept for the report; a position that does not
  land on a clock boundary is rounded to the nearest one (a device following 24 PPQN cannot do
  better, and the report says so).
* Only note messages are used. Tempo/CC/program changes are ignored: the host drives the tempo with
  its own clock pump, and the control surface owns the CCs.

The parser is read-only and allocates nothing per event at stream time: `MidiPattern.by_clock` is
built once when the file is loaded.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import mido

from hitl.control_constants import MIDI_CLOCKS_PER_BAR, MIDI_CLOCKS_PER_BEAT

#: The device's musical grid is 4/4; a pattern's default loop is rounded to whole bars of it.
BEATS_PER_BAR = 4


@dataclass(frozen=True)
class PatternEvent:
    """One note edge at a clock position inside the pattern."""

    clock: int
    note_on: bool
    note: int
    velocity: int


@dataclass(frozen=True)
class MidiPattern:
    path: Path
    ticks_per_beat: int
    length_ticks: int
    clock_length: int
    source_note_ons: int
    rounded_positions: int
    events: tuple[PatternEvent, ...]
    by_clock: dict[int, tuple[PatternEvent, ...]] = field(default_factory=dict)

    def events_at(self, clock: int) -> tuple[PatternEvent, ...]:
        """Events at one clock position, note-offs first so a wrap re-triggers cleanly."""
        return self.by_clock.get(int(clock), ())

    def bar_length_clocks(self) -> int:
        return MIDI_CLOCKS_PER_BAR

    def summary(self) -> dict[str, object]:
        return {
            "path": str(self.path),
            "ticks_per_beat": self.ticks_per_beat,
            "length_ticks": self.length_ticks,
            "clock_length": self.clock_length,
            "loop_beats": self.clock_length / MIDI_CLOCKS_PER_BEAT,
            "note_ons": self.source_note_ons,
            "events": len(self.events),
            # Positions the 24 PPQN grid cannot express exactly; rounded to the nearest clock.
            "rounded_positions": self.rounded_positions,
        }


def _read_absolute_notes(path: Path) -> tuple[int, list[tuple[int, bool, int, int]], int]:
    """Merge every track into absolute-tick note edges.

    A ``note_on`` with velocity 0 is a note-off (GM convention, and what Ableton writes for some
    exports). Returns ``(ticks_per_beat, edges, last_tick)``.
    """
    midi = mido.MidiFile(str(path))
    if midi.type == 2:
        raise ValueError(f"{path}: type-2 (asynchronous) MIDI files are not supported")
    ppq = int(midi.ticks_per_beat)
    if ppq <= 0:
        raise ValueError(f"{path}: SMPTE time division is not supported (only ticks-per-beat)")
    edges: list[tuple[int, bool, int, int]] = []
    last_tick = 0
    for track in midi.tracks:
        tick = 0
        for message in track:
            tick += int(message.time)
            if message.type == "note_on":
                edges.append((tick, bool(message.velocity > 0), int(message.note), int(message.velocity)))
            elif message.type == "note_off":
                edges.append((tick, False, int(message.note), int(message.velocity)))
        last_tick = max(last_tick, tick)
    return ppq, edges, last_tick


def load_midi_pattern(path: str | Path, *, loop_bars: Optional[int] = None) -> MidiPattern:
    """Parse one loop of a MIDI file into a clock pattern.

    ``loop_bars`` forces the loop length (the file must not contain events beyond it, or the wrap
    would move them). Without it the loop is the file's content rounded up to whole 4/4 bars.
    """
    file_path = Path(path)
    ppq, edges, last_tick = _read_absolute_notes(file_path)
    if not edges:
        raise ValueError(f"{file_path}: no note events")

    bar_ticks = BEATS_PER_BAR * ppq
    if loop_bars is not None and loop_bars > 0:
        length_ticks = int(loop_bars) * bar_ticks
        if last_tick > length_ticks:
            raise ValueError(
                f"{file_path}: {loop_bars} bar(s) = {length_ticks} ticks but the file runs to "
                f"{last_tick}; shorten the file or raise --loop-bars"
            )
    else:
        length_ticks = max(bar_ticks, int(math.ceil(last_tick / bar_ticks)) * bar_ticks)

    clocks_per_tick = MIDI_CLOCKS_PER_BEAT / float(ppq)
    clock_length = max(1, int(round(length_ticks * clocks_per_tick)))

    by_clock: dict[int, list[PatternEvent]] = {}
    note_ons = 0
    rounded_positions = 0
    for tick, is_on, note, velocity in edges:
        exact_clock = (tick % length_ticks) * clocks_per_tick
        clock = int(round(exact_clock)) % clock_length
        if abs(exact_clock - round(exact_clock)) > 1e-9:
            rounded_positions += 1
        event = PatternEvent(clock=clock, note_on=is_on, note=note,
                             velocity=velocity if is_on else 0)
        by_clock.setdefault(clock, []).append(event)
        if is_on:
            note_ons += 1

    frozen = {
        clock: tuple(sorted(group, key=lambda e: (e.note_on, e.note)))
        for clock, group in by_clock.items()
    }
    return MidiPattern(
        path=file_path,
        ticks_per_beat=ppq,
        length_ticks=length_ticks,
        clock_length=clock_length,
        source_note_ons=note_ons,
        rounded_positions=rounded_positions,
        events=tuple(event for group in frozen.values() for event in group),
        by_clock=frozen,
    )


def stream_pattern(
    out_port,
    master,
    pattern: MidiPattern,
    *,
    start_pulse: int,
    total_clocks: int,
    channel_1based: int,
    abort=None,
    wait_timeout_s: float = 2.0,
) -> dict[str, object]:
    """Emit ``pattern`` for ``total_clocks`` clocks from pulse ``start_pulse`` on the master grid.

    Every event is sent on the pulse it belongs to, so the device sees the pattern on its own
    clock grid rather than on host wall-clock jitter. Returns a small report for the run record.
    """
    channel = int(channel_1based) - 1
    sent = 0
    behind_pulses = 0
    for offset in range(max(int(total_clocks), 0)):
        pulse = int(start_pulse) + offset
        if not master.wait_pulse(pulse, timeout_s=wait_timeout_s, abort=abort):
            raise RuntimeError(
                f"clock pulse {pulse} was not sent in time (pump stopped or fell behind)")
        # How far the pump already is past this event's pulse: the honest measure of host pacing,
        # since a burst here means the note did not land on its own clock edge.
        behind_pulses = max(behind_pulses, master.pulses_sent() - (pulse + 1))
        position = offset % pattern.clock_length
        for event in pattern.events_at(position):
            if event.note_on:
                out_port.send(mido.Message("note_on", channel=channel, note=event.note,
                                           velocity=event.velocity or 100))
            else:
                out_port.send(mido.Message("note_off", channel=channel, note=event.note,
                                           velocity=0))
            sent += 1
    return {
        "pattern": pattern.summary(),
        "clocks": int(total_clocks),
        "events_sent": sent,
        "start_pulse": int(start_pulse),
        "behind_pulses": behind_pulses,
    }
