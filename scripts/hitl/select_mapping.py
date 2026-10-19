"""Pure firmware-compatible select mapping; logical +8192 is not a MIDI wire value."""
from __future__ import annotations

from hitl.control_constants import TICKS_PER_16TH_STEP

PITCHBEND_MIN = -8192
PITCHBEND_MAX = 8192
MIDI_PITCHBEND_MAX = 8191


def select_tick_from_pitchbend(
    pitch_value: int, nav_length: int, *, pitch_min: int = PITCHBEND_MIN, pitch_max: int = PITCHBEND_MAX
) -> int:
    if nav_length == 0:
        return 0
    num_steps = nav_length // TICKS_PER_16TH_STEP
    if num_steps == 0:
        return 0
    span = pitch_max - pitch_min
    step = 0
    if span != 0:
        step = ((pitch_value - pitch_min) * (num_steps - 1)) // span
    if step < 0:
        step = 0
    if step >= num_steps:
        step = num_steps - 1
    return step * TICKS_PER_16TH_STEP


def select_stacked_note_index_from_pitchbend(
    pitch_value: int,
    nav_length: int,
    stacked_count: int,
    *,
    pitch_min: int = PITCHBEND_MIN,
    pitch_max: int = PITCHBEND_MAX,
) -> int:
    if stacked_count <= 1:
        return 0
    num_steps = nav_length // TICKS_PER_16TH_STEP
    if num_steps <= 1:
        return 0
    span = pitch_max - pitch_min
    if span == 0:
        return 0
    pos = (pitch_value - pitch_min) * (num_steps - 1)
    rem = pos % span
    if rem < 0:
        rem += span
    idx = (rem * stacked_count) // span
    if idx >= stacked_count:
        return stacked_count - 1
    return idx


def pitchbend_for_step_and_stack(
    step: int,
    nav_length: int,
    stacked_index: int,
    stacked_count: int,
    *,
    pitch_min: int = PITCHBEND_MIN,
    pitch_max: int = PITCHBEND_MAX,
) -> int:
    """Return an exact legal-wire inverse, or refuse an unreachable step/rank."""
    num_steps = nav_length // TICKS_PER_16TH_STEP
    if not (0 <= step < num_steps and 0 <= stacked_index < stacked_count):
        raise ValueError(
            f"invalid step={step} stacked={stacked_index}/{stacked_count} nav={nav_length}"
        )
    target_tick = step * TICKS_PER_16TH_STEP
    search_min = max(pitch_min, PITCHBEND_MIN)
    search_max = min(pitch_max, MIDI_PITCHBEND_MAX)
    for pitch in range(search_min, search_max + 1):
        if select_tick_from_pitchbend(pitch, nav_length, pitch_min=pitch_min, pitch_max=pitch_max) != target_tick:
            continue
        if (
            select_stacked_note_index_from_pitchbend(
                pitch,
                nav_length,
                stacked_count,
                pitch_min=pitch_min,
                pitch_max=pitch_max,
            )
            == stacked_index
        ):
            return pitch
    raise ValueError(
        f"no pitchbend for step={step} stacked={stacked_index}/{stacked_count} nav={nav_length}"
    )
