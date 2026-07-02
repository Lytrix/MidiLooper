#!/usr/bin/env python3
"""Verify Ableton reference MIDI timing for note-edit motor fader bursts."""

from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path

import mido

REPO_ROOT = Path(__file__).resolve().parents[1]
FADER_MIDI_DIR = REPO_ROOT / "test" / "test_faders"

REFERENCE_FILES = (
    "fader2_pitchbend.mid",
    "fader3_CC2.mid",
    "fader4_CC3.mid",
)

EXPECTED_TICKS_PER_BEAT = 96
EXPECTED_INTER_POSITION_GAP_TICKS = 12
EXPECTED_MOTOR_NOTE_DURATION_TICKS = 24
EXPECTED_POSITION_SEND_COUNT = 3


@dataclass(frozen=True)
class MotorBurstTiming:
    inter_position_gap_ticks: int
    motor_note_duration_ticks: int
    position_send_count: int


def _collect_events(path: Path) -> list[tuple[int, mido.Message]]:
    midi = mido.MidiFile(path)
    events: list[tuple[int, mido.Message]] = []
    tick = 0
    for track in midi.tracks:
        for msg in track:
            tick += msg.time
            if msg.is_meta:
                continue
            events.append((tick, msg))
    return events


def _is_position_message(msg: mido.Message) -> bool:
    return msg.type in ("pitchwheel", "control_change")


def _extract_first_burst(events: list[tuple[int, mido.Message]]) -> MotorBurstTiming:
    position_ticks: list[int] = []
    note_on_tick: int | None = None
    note_off_tick: int | None = None

    for tick, msg in events:
        if _is_position_message(msg):
            if note_on_tick is None:
                position_ticks.append(tick)
            continue
        if msg.type == "note_on" and msg.velocity > 0 and note_on_tick is None:
            note_on_tick = tick
            continue
        if msg.type in ("note_off", "note_on") and note_on_tick is not None and note_off_tick is None:
            if msg.type == "note_off" or (msg.type == "note_on" and msg.velocity == 0):
                note_off_tick = tick
                break

    if len(position_ticks) < EXPECTED_POSITION_SEND_COUNT:
        raise AssertionError(f"expected >= {EXPECTED_POSITION_SEND_COUNT} position messages, got {position_ticks}")
    if note_on_tick is None or note_off_tick is None:
        raise AssertionError("missing note_on/note_off in first motor burst")

    first_three = position_ticks[:EXPECTED_POSITION_SEND_COUNT]
    gap1 = first_three[1] - first_three[0]
    gap2 = first_three[2] - first_three[1]
    if gap1 != gap2:
        raise AssertionError(f"uneven position gaps: {gap1}, {gap2}")
    if note_on_tick != first_three[-1]:
        raise AssertionError(
            f"note_on tick {note_on_tick} != last position tick {first_three[-1]}"
        )
    duration = note_off_tick - note_on_tick
    return MotorBurstTiming(gap1, duration, EXPECTED_POSITION_SEND_COUNT)


def main() -> int:
    for name in REFERENCE_FILES:
        path = FADER_MIDI_DIR / name
        midi = mido.MidiFile(path)
        if midi.ticks_per_beat != EXPECTED_TICKS_PER_BEAT:
            print(f"FAIL {name}: ticks_per_beat={midi.ticks_per_beat}", file=sys.stderr)
            return 1

        timing = _extract_first_burst(_collect_events(path))
        if timing.inter_position_gap_ticks != EXPECTED_INTER_POSITION_GAP_TICKS:
            print(
                f"FAIL {name}: inter_position_gap_ticks={timing.inter_position_gap_ticks}",
                file=sys.stderr,
            )
            return 1
        if timing.motor_note_duration_ticks != EXPECTED_MOTOR_NOTE_DURATION_TICKS:
            print(
                f"FAIL {name}: motor_note_duration_ticks={timing.motor_note_duration_ticks}",
                file=sys.stderr,
            )
            return 1
        if timing.position_send_count != EXPECTED_POSITION_SEND_COUNT:
            print(
                f"FAIL {name}: position_send_count={timing.position_send_count}",
                file=sys.stderr,
            )
            return 1
        print(
            f"OK {name}: positions={timing.position_send_count} "
            f"gap={timing.inter_position_gap_ticks}t note={timing.motor_note_duration_ticks}t"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
