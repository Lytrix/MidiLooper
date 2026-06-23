#!/usr/bin/env python3
"""HITL automation for note-edit overlap scenarios.

Prelude: clear track → standard 2-bar record with a deterministic fixture (not chromatic grid).
Long-over-short uses the doc 2-step record gates; M0 is lengthened in edit before move-over-P0.
Then one combined edit session exercising move/overlap, long-over-short same-pitch overlap with
pitch restore and move-past-higher-note display check, D delay move + pitch merge (64→67),
delete/insert/move-over-new-note reordering, add/remove via 16th double-tap, pitch change,
in-edit undo/redo, exit edit, and post-exit undo/redo verification.

Requires teensy41-capture-serial firmware and external serial capture for verified runs.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Callable, Optional

try:
    import mido
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "Missing dependency 'mido'. Install with:\n"
        "  python3 -m pip install mido python-rtmidi pyserial"
    ) from exc

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from host_midi_automation_baseline import (  # noqa: E402
    CONTROL_CHANNEL_1BASED,
    GLOBAL_TRANSPORT_NOTE,
    MIDI_CLOCKS_PER_BAR,
    RECORD_BUTTON_NOTE,
    RunAbort,
    SerialCaptureCollector,
    _clock_seen_within,
    _count_capture_state_entries,
    _count_capture_transitions,
    _drain_input_messages,
    _ensure_midi_clock,
    _extract_revt_note_on_ticks,
    _find_midi_port,
    _send_multi_short_press,
    _send_short_press,
    _wait_for_state_entry_count,
    _wait_for_transition_count,
)

DEFAULT_PERSISTENCE_WAIT_TIMEOUT_S = 30.0

# Canonical scoped edit-pass undo serial markers (TrackUndo post-exit global undo).
SCOPED_EDIT_PASS_UNDONE = "Scoped edit pass undone"
SCOPED_EDIT_PASS_REDONE = "Scoped edit pass redone"
_LEGACY_EDIT_PASS_UNDONE_MARKERS = (
    SCOPED_EDIT_PASS_UNDONE,
    "Note edit pass undone",
    "Note edit span undone",
)
_LEGACY_EDIT_PASS_REDONE_MARKERS = (
    SCOPED_EDIT_PASS_REDONE,
    "Note edit pass redone",
    "Note edit span redone",
)
_SERIAL_MARKER_ALIASES: dict[str, tuple[str, ...]] = {
    SCOPED_EDIT_PASS_UNDONE: _LEGACY_EDIT_PASS_UNDONE_MARKERS,
    SCOPED_EDIT_PASS_REDONE: _LEGACY_EDIT_PASS_REDONE_MARKERS,
}

# DROID main controls (MidiConfig::Transport / LengthEdit)
EDIT_BUTTON_NOTE = 38
LENGTH_EDIT_NOTE = 35
EDIT_ENTER_LOG_PATTERNS: tuple[str, ...] = (
    "MIDI Encoder: Short press - entered note edit mode",
    "Note edit: short press entered Select overlay",
)
FADER_SELECT_SETTLE_MS = 650
NOTE_SELECTION_GRACE_MS = 750
COARSE_EDIT_READY_MS = 1200
TICKS_PER_16TH_STEP = 48
TICKS_PER_BAR = 768
BEATS_PER_BAR = 4
SIXTEENTHS_PER_BEAT = 4
TICKS_PER_BEAT = TICKS_PER_BAR // BEATS_PER_BAR
BEAT_MOVE_STEPS = SIXTEENTHS_PER_BEAT
DISPLAY_SETTLE_MS = 800
M0_PITCH = 60
B_PITCH = 64
D_PITCH = 64
A_PITCH = 67
M0_STEP = 0
B_STEP = 4
A_STEP = 8
P0_STEP = 12  # same pitch as M0 (pitch-lane blocker)
D_STEP = 18
M0_POST_SANDWICH_STEP = 24  # last coarse move in sandwich leg
INSERT_AFTER_DELETE_STEP = D_STEP  # legacy alias for delay-move target
INSERT_NOTE_STEP = 1  # doc empty add-note target (step 18 often occupied after edits)
EDIT_BUTTON_DEBOUNCE_MS = 350  # firmware double-tap window before deferred short fires
POST_DELETE_QUIET_MS = 2500
# Long M0 end step (edit-time length extend) must span P0@step12 before move-over-short.
LONG_M0_END_FIXTURE_STEP = 14
LONG_OVER_P0_START_STEP = 10
# Empty step left of A@8 — move pitched note past higher A without landing on B@4.
PAST_HIGHER_NOTE_STEP = 2
# Recorded 2-step gate length in ticks (fixture default).
RECORD_GATE_TICKS = 2 * TICKS_PER_16TH_STEP

PITCHBEND_MIN = -8192
PITCHBEND_MAX = 8191

FADER_SELECT_CHANNEL_1BASED = 16  # Fader 1: note / empty-step selection only (PC1 + PB)
FADER_MOVE_CHANNEL_1BASED = 15    # Fader 2: coarse position move (PC2 + PB)
FADER_FINE_CC = 2
FADER_PITCH_CC = 3


@dataclass(frozen=True)
class FixtureNote:
    step: int
    pitch: int
    gate_steps: int = 2


# See docs/plans/m8_edit_note_edit_hitl_automation_refinement.md
EDIT_RECORD_FIXTURE: tuple[FixtureNote, ...] = (
    FixtureNote(0, 60),
    FixtureNote(4, 64),
    FixtureNote(8, 67),
    FixtureNote(12, 60),
    FixtureNote(18, 64),
    FixtureNote(22, 65),
    FixtureNote(26, 69),
)

# Three notes on step 8 — exercises fader-1 multi-note slot iteration.
EDIT_SAME_STEP_CHORD_FIXTURE: tuple[FixtureNote, ...] = (
    FixtureNote(0, 60),
    FixtureNote(8, 67),
    FixtureNote(8, 72),
    FixtureNote(8, 76),
    FixtureNote(16, 64),
)


@dataclass(frozen=True)
class SelectNavSlot:
    rel_tick: int
    note_idx: int = -1


@dataclass
class RecordLayout:
    """Select fader layout: one slot per note (multiple per 16th when notes share a step)."""

    loop_start: int
    loop_length: int
    step_to_tick: dict[int, int]
    nav_slots: list[SelectNavSlot] = field(default_factory=list)

    @property
    def nav_slot_count(self) -> int:
        return max(len(self.nav_slots), 1)

    @property
    def sixteenth_steps(self) -> int:
        return max(self.loop_length // TICKS_PER_16TH_STEP, 1)

    def sixteenth_step_for(self, step: int) -> int:
        """Fixture 16th step index for fader 2 coarse move grid."""
        if step in self.step_to_tick:
            return self.step_to_tick[step] // TICKS_PER_16TH_STEP
        return step


def _note_relative_tick(absolute_pos: int, loop_start: int, loop_length: int) -> int:
    if loop_length <= 0:
        return 0
    if absolute_pos >= loop_start:
        rel = absolute_pos - loop_start
    else:
        rel = absolute_pos + loop_length - loop_start
    return rel % loop_length


def _build_select_navigation_slots(
    revt_notes: list[tuple[int, int]],
    *,
    loop_length: int,
    loop_start: int = 0,
) -> list[SelectNavSlot]:
    """Mirror SelectNavigation::buildSelectNavigationSlots (NOTE_EDIT storage ticks, 0 origin)."""
    _ = loop_start  # RECS loop_start is display-only; REVT ticks are storage coordinates
    if loop_length <= 0:
        return []
    num_steps = loop_length // TICKS_PER_16TH_STEP
    slots: list[SelectNavSlot] = []
    for step in range(num_steps):
        step_tick = step * TICKS_PER_16TH_STEP
        notes_in_step: list[tuple[int, int]] = []
        for idx, (tick, _pitch) in enumerate(revt_notes):
            rel = tick % loop_length
            if rel // TICKS_PER_16TH_STEP == step:
                notes_in_step.append((rel, idx))
        notes_in_step.sort()
        if not notes_in_step:
            slots.append(SelectNavSlot(rel_tick=step_tick, note_idx=-1))
        else:
            for rel, idx in notes_in_step:
                slots.append(SelectNavSlot(rel_tick=rel, note_idx=idx))
    return slots


def _arduino_map(value: int, in_min: int, in_max: int, out_min: int, out_max: int) -> int:
    return (value - in_min) * (out_max - out_min) // (in_max - in_min) + out_min


def _pb_for_fader_index(index: int, count: int) -> int:
    """Minimum pitchbend that maps to index (Arduino map), avoids rounding to index+1."""
    if count <= 1:
        return 0
    lo, hi = PITCHBEND_MIN, PITCHBEND_MAX
    for pitch in range(lo, hi + 1):
        if _arduino_map(pitch, lo, hi, 0, count - 1) == index:
            return pitch
    return lo + index * (hi - lo) // (count - 1)


def _pb_for_sixteenth_step(step: int, num_steps: int) -> int:
    step = max(0, min(step, num_steps - 1))
    return _pb_for_fader_index(step, num_steps)


def _extract_revt_notes(lines: list[str]) -> list[tuple[int, int]]:
    """Stored note-on (tick, pitch) pairs from capture REVT lines."""
    recs_stop_ts: Optional[int] = None
    record_start_ts: Optional[int] = None
    for line in lines:
        if ",RECA," in line and record_start_ts is None:
            try:
                record_start_ts = int(line.split(",")[1])
            except ValueError:
                pass
        if ",RECS,stop," in line and recs_stop_ts is None:
            try:
                recs_stop_ts = int(line.split(",")[1])
            except ValueError:
                pass
            break
    if recs_stop_ts is None:
        return []

    notes: list[tuple[int, int]] = []
    for line in lines:
        if ",REVT," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 6:
            continue
        try:
            ts = int(parts[1])
            tick = int(parts[3])
            pitch = int(parts[5])
        except ValueError:
            continue
        if record_start_ts is not None and ts < record_start_ts:
            continue
        notes.append((tick, pitch))
    return notes


def _build_fixture_step_to_tick(
    revt_notes: list[tuple[int, int]],
    fixture: tuple[FixtureNote, ...],
    *,
    loop_length: int,
) -> dict[int, int]:
    """Map fixture 16th steps to stored loop ticks (REVT is loop-relative)."""
    num_steps = loop_length // TICKS_PER_16TH_STEP
    revt_by_pitch: dict[int, list[int]] = {}
    for tick, pitch in revt_notes:
        revt_by_pitch.setdefault(pitch, []).append(tick)

    step_to_tick: dict[int, int] = {}
    for fn in sorted(fixture, key=lambda n: (n.step, n.pitch)):
        ticks = revt_by_pitch.get(fn.pitch, [])
        if ticks:
            step_to_tick[fn.step] = ticks.pop(0)
        elif fn.step not in step_to_tick:
            step_to_tick[fn.step] = fn.step * TICKS_PER_16TH_STEP
    for step in range(num_steps):
        if step not in step_to_tick:
            step_to_tick[step] = step * TICKS_PER_16TH_STEP
    return step_to_tick


def _extract_last_recs_stop(lines: list[str]) -> Optional[dict[str, int]]:
    last: Optional[dict[str, int]] = None
    for line in lines:
        if ",RECS,stop," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 9:
            continue
        try:
            last = {
                "loop_start": int(parts[6]),
                "raw_length": int(parts[7]),
                "final_length": int(parts[8]),
            }
        except ValueError:
            continue
    return last


def _record_layout_from_serial(
    lines: list[str],
    *,
    record_bars: int,
    fixture: tuple[FixtureNote, ...],
) -> RecordLayout:
    recs = _extract_last_recs_stop(lines)
    revt_notes = _extract_revt_notes(lines)
    default_length = record_bars * TICKS_PER_BAR
    loop_length = recs["final_length"] if recs is not None else default_length
    if loop_length <= 0:
        loop_length = default_length
    loop_start = recs["loop_start"] if recs is not None else 0
    step_to_tick = _build_fixture_step_to_tick(revt_notes, fixture, loop_length=loop_length)
    nav_slots = _build_select_navigation_slots(
        revt_notes, loop_length=loop_length, loop_start=loop_start
    )
    if not step_to_tick:
        revt_notes = [(n.step * TICKS_PER_16TH_STEP, n.pitch) for n in fixture]
        loop_start = 0
        loop_length = default_length
        step_to_tick = _build_fixture_step_to_tick(revt_notes, fixture, loop_length=loop_length)
        nav_slots = _build_select_navigation_slots(
            revt_notes, loop_length=loop_length, loop_start=loop_start
        )
    return RecordLayout(
        loop_start=loop_start,
        loop_length=loop_length,
        step_to_tick=step_to_tick,
        nav_slots=nav_slots,
    )


def _wait_for_revt_count(
    collector: SerialCaptureCollector,
    *,
    min_count: int,
    timeout_s: float,
    abort: Optional[RunAbort],
) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        if abort is not None and abort.check() is not None:
            return False
        if len(_extract_revt_note_on_ticks(collector.snapshot())) >= min_count:
            return True
        time.sleep(0.05)
    return False


@dataclass
class EditScenario:
    id: str
    description: str
    run: Callable[..., None] = field(repr=False)
    serial_markers: tuple[str, ...] = ()


def _send_long_press(
    out_port: mido.ports.BaseOutput, *, note: int, channel_1based: int, press_ms: int
) -> None:
    ch = channel_1based - 1
    out_port.send(mido.Message("note_on", channel=ch, note=note, velocity=127))
    time.sleep(max(press_ms, 1) / 1000.0)
    out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))


def _send_double_press(
    out_port: mido.ports.BaseOutput,
    *,
    note: int,
    channel_1based: int,
    press_ms: int,
    gap_ms: int = 80,
) -> None:
    _send_multi_short_press(
        out_port,
        note=note,
        channel_1based=channel_1based,
        press_ms=press_ms,
        count=2,
        gap_ms=gap_ms,
    )


def _send_triple_press(
    out_port: mido.ports.BaseOutput,
    *,
    note: int,
    channel_1based: int,
    press_ms: int,
    gap_ms: int = 80,
) -> None:
    _send_multi_short_press(
        out_port,
        note=note,
        channel_1based=channel_1based,
        press_ms=press_ms,
        count=3,
        gap_ms=gap_ms,
    )


def _fader1_select_nav_slot_index(
    out_port: mido.ports.BaseOutput, *, layout: RecordLayout, slot_index: int
) -> None:
    """Fader 1: select navigation slot by index (supports multi-note steps)."""
    count = layout.nav_slot_count
    slot_index = max(0, min(slot_index, count - 1))
    _send_program_change(out_port, FADER_SELECT_CHANNEL_1BASED, 1)
    pb = _pb_for_fader_index(slot_index, count)
    out_port.send(
        mido.Message("pitchwheel", channel=FADER_SELECT_CHANNEL_1BASED - 1, pitch=pb)
    )
    time.sleep(FADER_SELECT_SETTLE_MS / 1000.0)
    time.sleep(max(NOTE_SELECTION_GRACE_MS - FADER_SELECT_SETTLE_MS, 0) / 1000.0)
    slot = layout.nav_slots[slot_index] if layout.nav_slots else None
    print(
        f"[edit-hitl] fader1 select slot={slot_index}/{count - 1} pb={pb} "
        f"rel_tick={getattr(slot, 'rel_tick', '?')} note_idx={getattr(slot, 'note_idx', '?')}"
    )


def _nav_slot_indices_for_fixture_step(layout: RecordLayout, fixture_step: int) -> list[int]:
    sixteenth = layout.sixteenth_step_for(fixture_step)
    return [
        i
        for i, slot in enumerate(layout.nav_slots)
        if slot.note_idx >= 0 and slot.rel_tick // TICKS_PER_16TH_STEP == sixteenth
    ]


def _insert_overlap_delete_line(line: str, *, insert_tick: int, tick_tolerance: int) -> bool:
    """True when serial line records a temporary delete of the insert note."""
    if f"pitch={M0_PITCH}" not in line:
        return False
    if not _line_has_start_near(line, insert_tick, tick_tolerance):
        return False
    return any(
        marker in line
        for marker in (
            "Stored deleted note",
            "Will delete note",
            "Will delete completely contained note",
            "Temporarily deleting MIDI event pair",
        )
    )


def _insert_mover_pitch_line(line: str) -> bool:
    return (
        f"Initialized moving note: pitch={M0_PITCH}," in line
        or f"Moving note: pitch={M0_PITCH}," in line
        or f"Using stable moving note identity: pitch={M0_PITCH}," in line
    )


def _nav_empty_slot_for_fixture_step(layout: RecordLayout, fixture_step: int) -> Optional[int]:
    """Nav slot index for an empty 16th (note_idx=-1) at fixture_step."""
    sixteenth = layout.sixteenth_step_for(fixture_step)
    for i, slot in enumerate(layout.nav_slots):
        if slot.note_idx < 0 and slot.rel_tick // TICKS_PER_16TH_STEP == sixteenth:
            return i
    return None


def _warmup_empty_fixture_step(layout: RecordLayout) -> Optional[int]:
    """Pick a fixture step with a real empty nav slot (never M0's step)."""
    preferred: list[int] = []
    for step in range(layout.sixteenth_steps):
        if step == M0_STEP:
            continue
        if _nav_empty_slot_for_fixture_step(layout, step) is not None:
            preferred.append(step)
    if not preferred:
        return None
    if INSERT_NOTE_STEP in preferred:
        return INSERT_NOTE_STEP
    return min(preferred)


def _fader1_select_empty_fixture_step(
    out_port: mido.ports.BaseOutput,
    *,
    layout: RecordLayout,
    fixture_step: int,
) -> bool:
    """Fader 1: bracket on empty grid step (required before NOTELEN create).

    Returns True when an empty nav slot was selected; False if no empty slot exists.
    """
    empty_slot = _nav_empty_slot_for_fixture_step(layout, fixture_step)
    if empty_slot is not None:
        _fader1_select_nav_slot_index(out_port, layout=layout, slot_index=empty_slot)
        print(f"[edit-hitl] fader1 select empty fixture_step={fixture_step} slot={empty_slot}")
        return True
    print(
        f"[edit-hitl] warn: no empty nav slot for fixture_step={fixture_step}; "
        "skipping grid fallback (would risk selecting a note)"
    )
    return False


def _fader1_select_sixteenth_step(
    out_port: mido.ports.BaseOutput,
    *,
    layout: RecordLayout,
    fixture_step: int,
    note_ordinal: int = 0,
) -> None:
    """Fader 1 only: move bracket to a 16th grid slot.

    When multiple notes share the step, note_ordinal selects which (0 = lowest pitch).
    """
    note_slots = _nav_slot_indices_for_fixture_step(layout, fixture_step)
    if note_slots:
        slot_index = note_slots[min(note_ordinal, len(note_slots) - 1)]
        _fader1_select_nav_slot_index(out_port, layout=layout, slot_index=slot_index)
        return

    sixteenth = layout.sixteenth_step_for(fixture_step)
    num_steps = layout.sixteenth_steps
    _send_program_change(out_port, FADER_SELECT_CHANNEL_1BASED, 1)
    pb = _pb_for_sixteenth_step(sixteenth, num_steps)
    out_port.send(
        mido.Message("pitchwheel", channel=FADER_SELECT_CHANNEL_1BASED - 1, pitch=pb)
    )
    time.sleep(FADER_SELECT_SETTLE_MS / 1000.0)
    time.sleep(max(NOTE_SELECTION_GRACE_MS - FADER_SELECT_SETTLE_MS, 0) / 1000.0)
    print(
        f"[edit-hitl] fader1 select fixture_step={fixture_step} "
        f"sixteenth={sixteenth}/{num_steps - 1} pb={pb} (fallback)"
    )


def _fader1_select_fixture_note(
    out_port: mido.ports.BaseOutput,
    *,
    layout: RecordLayout,
    fixture_step: int,
    note_ordinal: int = 0,
    tick_tolerance: int = 8,
) -> None:
    """Select the recorded fixture note by storage tick (not just 16th grid bucket)."""
    target_tick = layout.step_to_tick.get(
        fixture_step, fixture_step * TICKS_PER_16TH_STEP
    )
    matching = [
        i
        for i, slot in enumerate(layout.nav_slots)
        if slot.note_idx >= 0
        and abs(slot.rel_tick - target_tick) <= tick_tolerance
    ]
    if matching:
        idx = matching[min(note_ordinal, len(matching) - 1)]
        _fader1_select_nav_slot_index(out_port, layout=layout, slot_index=idx)
        return
    _fader1_select_sixteenth_step(
        out_port,
        layout=layout,
        fixture_step=fixture_step,
        note_ordinal=note_ordinal,
    )


def _fader1_select_then_wait_for_fader2(
    out_port: mido.ports.BaseOutput,
    *,
    layout: RecordLayout,
    fixture_step: int,
    note_ordinal: int = 0,
) -> None:
    """Select with fader 1, then wait until coarse move (fader 2) is allowed."""
    _fader1_select_fixture_note(out_port, layout=layout, fixture_step=fixture_step, note_ordinal=note_ordinal)
    time.sleep(COARSE_EDIT_READY_MS / 1000.0)


def _fader2_move_to_sixteenth_step(
    out_port: mido.ports.BaseOutput,
    *,
    layout: RecordLayout,
    fixture_step: int,
) -> None:
    """Fader 2 only: move the currently selected note to target 16th step."""
    sixteenth = layout.sixteenth_step_for(fixture_step)
    num_steps = layout.sixteenth_steps
    _send_program_change(out_port, FADER_MOVE_CHANNEL_1BASED, 2)
    pb = _pb_for_sixteenth_step(sixteenth, num_steps)
    out_port.send(
        mido.Message("pitchwheel", channel=FADER_MOVE_CHANNEL_1BASED - 1, pitch=pb)
    )
    print(
        f"[edit-hitl] fader2 move fixture_step={fixture_step} "
        f"sixteenth={sixteenth}/{num_steps - 1} pb={pb}"
    )
    time.sleep(COARSE_EDIT_READY_MS / 1000.0)


def _notelen_delete_or_create_note(
    out_port: mido.ports.BaseOutput, *, press_ms: int, gap_ms: int = 80
) -> None:
    """NOTELEN (ch16 note 35) double: delete selected note or create at empty bracket."""
    _send_double_press(
        out_port,
        note=LENGTH_EDIT_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
        gap_ms=gap_ms,
    )


def _delete_selected_note(
    out_port: mido.ports.BaseOutput, *, press_ms: int, gap_ms: int = 80
) -> None:
    _notelen_delete_or_create_note(out_port, press_ms=press_ms, gap_ms=gap_ms)


def _create_note_at_bracket(
    out_port: mido.ports.BaseOutput, *, press_ms: int, gap_ms: int = 80
) -> None:
    _notelen_delete_or_create_note(out_port, press_ms=press_ms, gap_ms=gap_ms)


def _send_program_change(out_port: mido.ports.BaseOutput, channel_1based: int, program: int) -> None:
    out_port.send(
        mido.Message("program_change", channel=channel_1based - 1, program=program & 0x7F)
    )


def _toggle_length_edit_mode(out_port: mido.ports.BaseOutput, *, press_ms: int) -> None:
    """Short-press length-edit toggle (ch16 note 35)."""
    _send_short_press(
        out_port,
        note=LENGTH_EDIT_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    time.sleep(COARSE_EDIT_READY_MS / 1000.0)


def _coarse_pause() -> None:
    time.sleep(COARSE_EDIT_READY_MS / 1000.0)


def _display_pause(phase_wait_ms: int) -> None:
    time.sleep(max(DISPLAY_SETTLE_MS, phase_wait_ms) / 1000.0)


def _extend_m0_for_long_over_short(
    out_port: mido.ports.BaseOutput,
    *,
    layout: RecordLayout,
    press_ms: int,
    phase_wait_ms: int,
    end_fixture_step: int = LONG_M0_END_FIXTURE_STEP,
) -> None:
    """Lengthen recorded M0 in edit so it can contain short P0 (record uses 2-step gates)."""
    print(
        f"[edit-hitl] lengthen M0 end to fixture step {end_fixture_step} "
        f"(length edit, record gate unchanged)"
    )
    _fader1_select_then_wait_for_fader2(out_port, layout=layout, fixture_step=M0_STEP)
    time.sleep(max(phase_wait_ms, 1) / 1000.0)
    _toggle_length_edit_mode(out_port, press_ms=press_ms)
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=end_fixture_step)
    time.sleep(max(DISPLAY_SETTLE_MS, phase_wait_ms) / 1000.0)
    _toggle_length_edit_mode(out_port, press_ms=press_ms)


def _fader4_pitch_cc(out_port: mido.ports.BaseOutput, value: int) -> None:
    """Fader 4: pitch CC on ch15 (requires note already selected via fader 1)."""
    out_port.send(
        mido.Message(
            "control_change",
            channel=FADER_MOVE_CHANNEL_1BASED - 1,
            control=FADER_PITCH_CC,
            value=max(0, min(127, value)),
        )
    )


def _stream_fixture_record(
    out_port: mido.ports.BaseOutput,
    in_port: mido.ports.BaseInput,
    *,
    fixture: tuple[FixtureNote, ...],
    target_bars: int,
    midi_channel_1based: int,
    step_clocks: int = 6,
    stop_press_advance_clocks: int = 0,
    press_ms: int = 120,
    abort: Optional[RunAbort] = None,
) -> tuple[int, int]:
    """Emit fixture notes on MIDI clock for target_bars."""
    ch = midi_channel_1based - 1
    fixture_by_step: dict[int, list[FixtureNote]] = {}
    for n in fixture:
        fixture_by_step.setdefault(n.step, []).append(n)
    for step_notes in fixture_by_step.values():
        step_notes.sort(key=lambda n: n.pitch)
    target_clocks = target_bars * MIDI_CLOCKS_PER_BAR
    stop_trigger: Optional[int] = None
    if stop_press_advance_clocks > 0:
        stop_trigger = max(1, target_clocks - stop_press_advance_clocks)
    stop_sent = False
    phase_clock = 0
    note_on_count = 0
    held: list[tuple[int, int]] = []
    next_emit_step = 0

    while True:
        msg = in_port.poll()
        if msg is None:
            break
        if msg.type in ("start", "stop", "continue", "clock"):
            continue

    while phase_clock < target_clocks:
        if abort is not None and abort.check() is not None:
            break
        msg = in_port.poll()
        if msg is None:
            time.sleep(0.0005)
            continue
        if msg.type != "clock":
            continue
        phase_clock += 1

        still: list[tuple[int, int]] = []
        for note, off_at in held:
            if phase_clock >= off_at:
                out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))
            else:
                still.append((note, off_at))
        held = still

        current_16th = (phase_clock + step_clocks - 1) // step_clocks - 1
        while next_emit_step <= current_16th and next_emit_step < target_bars * 16:
            entries = fixture_by_step.get(next_emit_step, [])
            for entry in entries:
                # Monophonic MIDI: close any still-held note before retriggering same pitch.
                still_held: list[tuple[int, int]] = []
                for note, off_at in held:
                    if note == entry.pitch:
                        out_port.send(
                            mido.Message("note_off", channel=ch, note=note, velocity=0)
                        )
                    else:
                        still_held.append((note, off_at))
                held = still_held
                out_port.send(
                    mido.Message("note_on", channel=ch, note=entry.pitch, velocity=98)
                )
                gate_clocks = entry.gate_steps * step_clocks
                held.append((entry.pitch, phase_clock + gate_clocks))
                note_on_count += 1
            next_emit_step += 1

        if (
            stop_trigger is not None
            and not stop_sent
            and phase_clock >= stop_trigger
        ):
            _send_short_press(
                out_port,
                note=RECORD_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=press_ms,
            )
            stop_sent = True

    return note_on_count, phase_clock


def _count_reca_markers(lines: list[str]) -> int:
    return sum(1 for line in lines if ",RECA," in line)


def _wait_for_recording_start(
    collector: SerialCaptureCollector,
    *,
    baseline_reca: int,
    baseline_recording_transitions: int,
    timeout_s: float,
    abort: Optional[RunAbort],
) -> bool:
    """Wait for capture evidence that record actually started.

    Never send a second record press on timeout — that toggles stop while recording.
    """
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        if abort is not None and abort.check() is not None:
            return False
        lines = collector.snapshot()
        if _count_reca_markers(lines) > baseline_reca:
            return True
        if any("Recording started" in line for line in lines):
            return True
        transitions = _count_capture_transitions(lines)
        recording_transitions = sum(
            count for (from_state, to_state), count in transitions.items()
            if to_state == "RECORDING"
        )
        if recording_transitions > baseline_recording_transitions:
            return True
        time.sleep(0.01)
    return False


def _recording_transition_baseline(lines: list[str]) -> int:
    return sum(
        count
        for (from_state, to_state), count in _count_capture_transitions(lines).items()
        if to_state == "RECORDING"
    )


def _is_edit_enter_line(line: str) -> bool:
    return any(pattern in line for pattern in EDIT_ENTER_LOG_PATTERNS)


def _find_edit_enter_index(lines: list[str]) -> Optional[int]:
    for i, line in enumerate(lines):
        if _is_edit_enter_line(line):
            return i
    return None


def _find_session_enter_anchor(lines: list[str]) -> Optional[int]:
    enter_idx = _find_edit_enter_index(lines)
    if enter_idx is not None:
        return enter_idx
    for i, line in enumerate(lines):
        if "EditSession opened editPass=0" in line:
            return i
    return None


def _verify_session_state_enter(lines: list[str]) -> dict[str, object]:
    """Enter press lands Select kind; must not cycle geometry kind on the same press."""
    issues: list[str] = []
    anchor_idx = _find_session_enter_anchor(lines)
    if anchor_idx is None:
        issues.append("session_state:enter_log_missing")
        return {"ok": False, "issues": issues, "enter_index": None}

    window = lines[anchor_idx : anchor_idx + 24]
    if not any(_is_edit_enter_line(line) for line in window):
        issues.append("session_state:enter_log_missing")

    select_seen = False
    cycle_before_select = False
    for line in window:
        if "Entered SELECT mode" in line or "edit mode program: 1" in line:
            select_seen = True
        if not select_seen and "Note edit type cycled to kind=" in line:
            cycle_before_select = True
        if not select_seen and "Entered EditStartNoteState" in line:
            cycle_before_select = True

    if cycle_before_select:
        issues.append("session_state:cycle_on_enter_press")
    if not select_seen:
        issues.append("session_state:select_kind_missing_at_enter")
    return {
        "ok": not issues,
        "issues": issues,
        "enter_index": anchor_idx,
        "select_seen": select_seen,
        "cycle_on_enter": cycle_before_select,
    }


_DISP_RECORDING_RE = re.compile(
    r",DISP,0,RECORDING,(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)"
)


def _verify_live_record_display(lines: list[str], *, min_loop_len: int = 48) -> dict[str, object]:
    """During RECORD, piano-roll frameNotes should appear while capture events grow."""
    issues: list[str] = []
    reca_idx: Optional[int] = None
    record_end_idx: Optional[int] = None
    for i, line in enumerate(lines):
        if ",RECA," in line and reca_idx is None:
            reca_idx = i
        if reca_idx is not None and ",ST,Track,RECORDING,STOPPED_RECORDING" in line:
            record_end_idx = i
            break
    if reca_idx is None:
        issues.append("live_record_display:reca_missing")
        return {"ok": False, "issues": issues}

    window = lines[reca_idx : record_end_idx if record_end_idx is not None else len(lines)]
    rows: list[tuple[int, int, int, int, int, int]] = []
    for line in window:
        m = _DISP_RECORDING_RE.search(line)
        if m:
            rows.append(tuple(int(g) for g in m.groups()))

    frame_positive = [
        r for r in rows if r[3] > 0 and r[1] > 0 and r[0] >= min_loop_len
    ]
    sustained_zero = 0
    max_sustained_zero = 0
    for loop_len, take, _visual, frame, _buf, _pub in rows:
        if take > 0 and frame == 0 and loop_len >= min_loop_len:
            sustained_zero += 1
            max_sustained_zero = max(max_sustained_zero, sustained_zero)
        else:
            sustained_zero = 0

    if rows and not frame_positive:
        issues.append("live_record_display:no_frame_notes_during_record")
    if max_sustained_zero >= 5:
        issues.append(f"live_record_display:sustained_frame_zero:{max_sustained_zero}")

    return {
        "ok": not issues,
        "issues": issues,
        "disp_samples": len(rows),
        "frame_positive_samples": len(frame_positive),
        "max_sustained_frame_zero": max_sustained_zero,
    }


def _verify_warmup_empty_nav_create(lines: list[str]) -> dict[str, object]:
    """First empty fader-1 nav after enter must allow NOTELEN create (not stale delete)."""
    issues: list[str] = []
    enter_idx = _find_session_enter_anchor(lines)
    if enter_idx is None:
        issues.append("warmup_create:enter_anchor_missing")
        return {"ok": False, "issues": issues}

    empty_idx: Optional[int] = None
    for i in range(enter_idx + 1, len(lines)):
        if "selected empty step at tick" in lines[i]:
            empty_idx = i
            break
    if empty_idx is None:
        issues.append("warmup_create:empty_nav_missing")
        return {"ok": False, "issues": issues, "empty_nav_index": None}

    notelen_double_idx: Optional[int] = None
    notelen_action: Optional[str] = None
    search_end = min(empty_idx + 150, len(lines))
    for i in range(empty_idx + 1, search_end):
        if "Button press: Length Edit Mode (double)" not in lines[i]:
            continue
        notelen_double_idx = i
        window = lines[i : i + 4]
        if any("NOTELEN double: create note at bracket" in line for line in window):
            notelen_action = "create"
        elif any("NOTELEN double: delete selected note" in line for line in window):
            notelen_action = "delete"
        break

    if notelen_double_idx is None:
        issues.append("warmup_create:notelen_double_missing")
    elif notelen_action != "create":
        issues.append(f"warmup_create:notelen_action_{notelen_action or 'unknown'}")

    created_seen = any(
        "Created 32nd note" in line for line in lines[empty_idx:search_end]
    )
    if not created_seen:
        issues.append("warmup_create:created_32nd_missing")

    return {
        "ok": not issues,
        "issues": issues,
        "empty_nav_index": empty_idx,
        "notelen_double_index": notelen_double_idx,
        "notelen_action": notelen_action,
        "created_seen": created_seen,
    }


def _verify_session_undo_redo_routing(lines: list[str]) -> dict[str, object]:
    """In-edit undo/redo uses NoteEditSession stack; post-exit uses pass undo."""
    issues: list[str] = []
    enter_idx = _find_edit_enter_index(lines)
    exit_idx: Optional[int] = None
    if enter_idx is not None:
        for i in range(enter_idx + 1, len(lines)):
            if "exited edit mode" in lines[i]:
                exit_idx = i
                break

    in_edit_undo = 0
    in_edit_redo = 0
    post_exit_undo = 0
    post_exit_redo = 0
    if enter_idx is not None and exit_idx is not None:
        in_window = lines[enter_idx:exit_idx]
        post_window = lines[exit_idx:]
        in_edit_undo = sum(1 for line in in_window if "EditSession undo" in line)
        in_edit_redo = sum(1 for line in in_window if "EditSession redo" in line)
        post_exit_undo = sum(
            1
            for line in post_window
            if _line_is_scoped_edit_pass_undo(line) or "Overdub undone" in line
        )
        post_exit_redo = sum(
            1
            for line in post_window
            if _line_is_scoped_edit_pass_redo(line) or "Overdub redone" in line
        )
        if in_edit_undo < 1:
            issues.append("session_undo:in_edit_missing")
        if in_edit_redo < 1:
            issues.append("session_redo:in_edit_missing")
        if post_exit_undo < 1:
            issues.append("session_undo:post_exit_missing")
        if post_exit_redo < 1:
            issues.append("session_redo:post_exit_missing")
    else:
        issues.append("session_undo:enter_or_exit_window_missing")

    return {
        "ok": not issues,
        "issues": issues,
        "in_edit_undo": in_edit_undo,
        "in_edit_redo": in_edit_redo,
        "post_exit_undo": post_exit_undo,
        "post_exit_redo": post_exit_redo,
    }


def _serial_contains(lines: list[str], needle: str) -> bool:
    return any(needle in line for line in lines)


def _serial_contains_any(lines: list[str], needles: tuple[str, ...]) -> bool:
    return any(needle in line for line in lines for needle in needles)


def _count_serial_substrings(lines: list[str], needle: str) -> int:
    return sum(1 for line in lines if needle in line)


def _count_serial_substrings_any(lines: list[str], needles: tuple[str, ...]) -> int:
    return sum(1 for line in lines if any(needle in line for needle in needles))


def _line_is_scoped_edit_pass_undo(line: str) -> bool:
    return any(marker in line for marker in _LEGACY_EDIT_PASS_UNDONE_MARKERS)


def _line_is_scoped_edit_pass_redo(line: str) -> bool:
    return any(marker in line for marker in _LEGACY_EDIT_PASS_REDONE_MARKERS)


def _serial_marker_found(lines: list[str], marker: str) -> bool:
    aliases = _SERIAL_MARKER_ALIASES.get(marker, (marker,))
    return _serial_contains_any(lines, aliases)


def _serial_has_clear_ignored_empty(lines: list[str], *, after_index: int = 0) -> bool:
    return any(
        "Clear ignored — track is empty" in line for line in lines[after_index:]
    )


def _serial_has_track_state(lines: list[str]) -> bool:
    return any(",ST,Track," in line for line in lines)


def _serial_suggests_loop_content(lines: list[str]) -> bool:
    """True when serial shows an active or committed loop (not safe to skip clear)."""
    if _extract_revt_note_on_ticks(lines):
        return True
    latest = _latest_track_state(lines)
    if latest in ("RECORDING", "PLAYING", "OVERDUBBING", "STOPPED_RECORDING"):
        return True
    if latest == "STOPPED":
        recs = _extract_last_recs_stop(lines)
        if recs is not None and int(recs.get("final_length", 0)) != 0:
            return True
    return False


def _can_skip_clear_for_record(lines: list[str]) -> bool:
    """Idle / empty track: no clear long-press needed before arm/record."""
    latest = _latest_track_state(lines)
    if latest == "EMPTY":
        return True
    if _serial_has_clear_ignored_empty(lines):
        return True
    if not _serial_has_track_state(lines) and not _serial_suggests_loop_content(lines):
        return True
    return False


def _track_cleared_for_record(lines: list[str]) -> bool:
    """EMPTY, ARMED without loop content, or STOPPED with no committed loop length."""
    latest = _latest_track_state(lines)
    if latest == "EMPTY":
        return True
    if latest == "ARMED":
        return not _serial_suggests_loop_content(lines)
    if latest == "STOPPED":
        recs = _extract_last_recs_stop(lines)
        if recs is not None and int(recs.get("final_length", 0)) != 0:
            return False
        if _extract_revt_note_on_ticks(lines):
            return False
        return True
    return False


def _latest_track_state(lines: list[str]) -> Optional[str]:
    latest: Optional[str] = None
    for line in lines:
        if ",ST,Track," not in line:
            continue
        tail = line.split(",ST,Track,", 1)[1]
        parts = tail.split(",")
        if len(parts) >= 2:
            latest = parts[1].strip()
    return latest


def _ensure_transport_running(
    out_port: mido.ports.BaseOutput,
    in_port: mido.ports.BaseInput,
    *,
    press_ms: int,
    phase_wait_ms: int,
) -> bool:
    """Start transport only when the host is not already receiving MIDI clock."""
    if _clock_seen_within(in_port, 0.5):
        return True
    for attempt in range(1, 4):
        _send_short_press(
            out_port,
            note=GLOBAL_TRANSPORT_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=press_ms,
        )
        time.sleep(phase_wait_ms / 1000.0)
        if _clock_seen_within(in_port, 1.0):
            return True
        print(f"[warn] No MIDI clock after transport start (attempt {attempt}/3)")
    return _clock_seen_within(in_port, 1.0)


def _stop_transport_if_running(
    out_port: mido.ports.BaseOutput,
    in_port: mido.ports.BaseInput,
    *,
    press_ms: int,
    phase_wait_ms: int,
) -> bool:
    """Toggle transport off when the host already sees MIDI clock."""
    if not _clock_seen_within(in_port, 0.5):
        return False
    print("[edit-hitl] transport stop before clear/record precondition")
    _send_short_press(
        out_port,
        note=GLOBAL_TRANSPORT_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    time.sleep(phase_wait_ms / 1000.0)
    return True


def _ensure_clear_to_empty(
    out_port: mido.ports.BaseOutput,
    serial_collector: SerialCaptureCollector,
    *,
    clear_press_ms: int,
    state_sync_timeout_ms: int,
    abort: Optional[RunAbort],
) -> bool:
    """Long-press clear until serial confirms EMPTY (or already-cleared track)."""
    snap = serial_collector.snapshot()
    if _can_skip_clear_for_record(snap):
        latest = _latest_track_state(snap)
        print(
            f"[edit-hitl] track ready for record (latest={latest}); "
            "skipping clear — proceed to arm/record"
        )
        return True

    print("[edit-hitl] clear selected loop (long press record)")
    baseline_len = len(snap)
    state_counts = _count_capture_state_entries(snap)
    expected_empty_count = state_counts.get("EMPTY", 0) + 1
    _send_short_press(
        out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=clear_press_ms,
    )
    reached_empty = _wait_for_state_entry_count(
        serial_collector,
        to_state="EMPTY",
        target_count=expected_empty_count,
        timeout_s=state_sync_timeout_ms / 1000.0,
        abort=abort,
    )
    post_snap = serial_collector.snapshot()
    if not reached_empty and (
        _serial_has_clear_ignored_empty(post_snap, after_index=baseline_len)
        or _serial_has_clear_ignored_empty(post_snap)
    ):
        print("[info] Clear ignored on already-empty track; precondition satisfied")
        reached_empty = True
    if not reached_empty and _track_cleared_for_record(post_snap):
        latest = _latest_track_state(post_snap)
        print(
            f"[info] Track cleared for record without EMPTY transition (latest={latest})"
        )
        reached_empty = True
    if not reached_empty:
        print("[warn] Timed out waiting for clear->EMPTY; retrying clear long press")
        retry_baseline_len = len(serial_collector.snapshot())
        _send_short_press(
            out_port,
            note=RECORD_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=clear_press_ms,
        )
        reached_empty = _wait_for_state_entry_count(
            serial_collector,
            to_state="EMPTY",
            target_count=expected_empty_count,
            timeout_s=state_sync_timeout_ms / 1000.0,
            abort=abort,
        )
        post_retry = serial_collector.snapshot()
        if not reached_empty and (
            _serial_has_clear_ignored_empty(post_retry, after_index=retry_baseline_len)
            or _serial_has_clear_ignored_empty(post_retry)
        ):
            print("[info] Clear ignored on already-empty track after retry")
            reached_empty = True
        if not reached_empty and _track_cleared_for_record(post_retry):
            latest = _latest_track_state(post_retry)
            print(
                f"[info] Track cleared for record after retry (latest={latest})"
            )
            reached_empty = True
    if reached_empty:
        latest = _latest_track_state(serial_collector.snapshot())
        print(f"[edit-hitl] clear confirmed (latest track state={latest})")
    return reached_empty


def _ensure_recording_started(
    out_port: mido.ports.BaseOutput,
    serial_collector: SerialCaptureCollector,
    *,
    press_ms: int,
    state_sync_timeout_ms: int,
    abort: Optional[RunAbort],
) -> bool:
    """Arm then record: EMPTY -> ARMED (transport off), then ARMED -> RECORDING starts transport."""
    snap = serial_collector.snapshot()
    baseline_reca = _count_reca_markers(snap)
    baseline_recording_transitions = _recording_transition_baseline(snap)
    state_counts = _count_capture_state_entries(snap)
    expected_recording_count = state_counts.get("RECORDING", 0) + 1
    latest = _latest_track_state(snap)
    if latest not in (None, "EMPTY", "ARMED", "STOPPED"):
        print(
            f"[warn] Record precondition: expected EMPTY/ARMED/STOPPED before record press, "
            f"latest state={latest}"
        )

    _send_short_press(
        out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )

    timeout_s = state_sync_timeout_ms / 1000.0
    reached = _wait_for_recording_start(
        serial_collector,
        baseline_reca=baseline_reca,
        baseline_recording_transitions=baseline_recording_transitions,
        timeout_s=timeout_s,
        abort=abort,
    )
    if reached:
        latest = _latest_track_state(serial_collector.snapshot())
        print(f"[edit-hitl] recording started (latest track state={latest})")
        return True

    print("[warn] Record did not start; retrying record press (not yet in RECORDING)")
    _send_short_press(
        out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    reached = _wait_for_recording_start(
        serial_collector,
        baseline_reca=baseline_reca,
        baseline_recording_transitions=baseline_recording_transitions,
        timeout_s=timeout_s,
        abort=abort,
    )
    if not reached:
        reached = _wait_for_state_entry_count(
            serial_collector,
            to_state="RECORDING",
            target_count=expected_recording_count,
            timeout_s=timeout_s,
            abort=abort,
        )
    if reached:
        latest = _latest_track_state(serial_collector.snapshot())
        print(f"[edit-hitl] recording started (latest track state={latest})")
    return reached


def _send_global_undo(
    out_port: mido.ports.BaseOutput, *, press_ms: int, label: str = "undo"
) -> None:
    print(f"[edit-hitl] global {label} (double press record)")
    _send_double_press(
        out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )


def _send_global_redo(
    out_port: mido.ports.BaseOutput, *, press_ms: int, label: str = "redo"
) -> None:
    print(f"[edit-hitl] global {label} (triple press record)")
    _send_triple_press(
        out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )


def _persistence_result_ok(line: str) -> bool:
    if ",PERS,result," not in line:
        return False
    parts = line.split(",")
    return len(parts) >= 8 and parts[7].strip() == "ok"


def _wait_for_persistence_result_after_marker(
    collector: SerialCaptureCollector,
    *,
    marker: str,
    timeout_s: float = DEFAULT_PERSISTENCE_WAIT_TIMEOUT_S,
    abort: Optional[RunAbort] = None,
) -> bool:
    """Wait for #CAP PERS,result,ok queued after edit exit (or other anchor line)."""
    deadline = time.monotonic() + max(timeout_s, 0.0)
    anchor_idx = -1
    while time.monotonic() < deadline:
        if abort is not None:
            reason = abort.check()
            if reason:
                print(f"[warn] persistence wait aborted: {reason}")
                return False
        lines = collector.snapshot()
        for i, line in enumerate(lines):
            if marker in line:
                anchor_idx = max(anchor_idx, i)
        if anchor_idx >= 0:
            for line in lines[anchor_idx + 1 :]:
                if _persistence_result_ok(line):
                    return True
        time.sleep(0.05)
    return False


def _undo_redo_pair(
    out_port: mido.ports.BaseOutput,
    *,
    press_ms: int,
    undo_redo_delay_ms: int,
    phase: str,
) -> None:
    gap_s = max(undo_redo_delay_ms, 0) / 1000.0
    time.sleep(gap_s)
    _send_global_undo(out_port, press_ms=press_ms, label=f"{phase} undo")
    time.sleep(gap_s)
    _send_global_redo(out_port, press_ms=press_ms, label=f"{phase} redo")
    time.sleep(gap_s)


def _parse_position_edits(lines: list[str]) -> list[dict[str, int]]:
    edits: list[dict[str, int]] = []
    pattern = re.compile(
        r"POSITION EDIT: Note moved from step (\d+) to (\d+) "
        r"\(tick (\d+) -> (\d+), relative (\d+) -> (\d+)\)"
    )
    for line in lines:
        match = pattern.search(line)
        if not match:
            continue
        from_step, to_step, old_tick, new_tick, old_rel, new_rel = (
            int(x) for x in match.groups()
        )
        if old_tick == new_tick and from_step == to_step:
            continue
        edits.append(
            {
                "from_step": from_step,
                "to_step": to_step,
                "old_tick": old_tick,
                "new_tick": new_tick,
                "old_rel": old_rel,
                "new_rel": new_rel,
                "delta_tick": new_tick - old_tick,
                "delta_step": to_step - from_step,
            }
        )
    return edits


def _parse_moved_note_events(lines: list[str]) -> list[dict[str, int]]:
    events: list[dict[str, int]] = []
    pattern = re.compile(
        r"Moved note events: pitch=(\d+) start->(\d+) end->(\d+)"
    )
    for line in lines:
        match = pattern.search(line)
        if not match:
            continue
        pitch, start, end = (int(x) for x in match.groups())
        events.append({"pitch": pitch, "start": start, "end": end})
    return events


def _parse_dnte_lines(lines: list[str]) -> list[dict[str, int]]:
    """OLED note-info capture (#CAP,...,DNTE,pitch,storage,display,length,selIdx)."""
    notes: list[dict[str, int]] = []
    for line in lines:
        if ",DNTE," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 8:
            continue
        try:
            notes.append(
                {
                    "pitch": int(parts[3]),
                    "storage_start": int(parts[4]),
                    "display_start": int(parts[5]),
                    "length": int(parts[6]),
                    "selected_idx": int(parts[7]),
                }
            )
        except ValueError:
            continue
    return notes


def _parse_final_notes(lines: list[str]) -> list[dict[str, int]]:
    notes: list[dict[str, int]] = []
    pattern = re.compile(
        r"Final note: pitch=(\d+), start=(\d+), end=(\d+)"
    )
    for line in lines:
        match = pattern.search(line)
        if not match:
            continue
        pitch, start, end = (int(x) for x in match.groups())
        notes.append({"pitch": pitch, "start": start, "end": end})
    return notes


def _parse_loop_length(lines: list[str]) -> int:
    pattern = re.compile(r"Reconstructing notes with loop length: (\d+)")
    for line in lines:
        match = pattern.search(line)
        if match:
            return int(match.group(1))
    return 1536


def _note_span_length(start: int, end: int, loop_length: int) -> int:
    if end >= start:
        return end - start
    return (loop_length - start) + end


def _note_key(pitch: int, start: int) -> tuple[int, int]:
    return (pitch, start)


def _inventory_from_notes(
    notes: list[dict[str, int]], loop_length: int
) -> dict[tuple[int, int], dict[str, int]]:
    inv: dict[tuple[int, int], dict[str, int]] = {}
    for note in notes:
        key = _note_key(note["pitch"], note["start"])
        inv[key] = {
            "pitch": note["pitch"],
            "start": note["start"],
            "end": note["end"],
            "length": _note_span_length(note["start"], note["end"], loop_length),
        }
    return inv


def _extract_reconstruction_snapshots(
    lines: list[str], loop_length: int
) -> list[dict[str, object]]:
    """Capture each post-reconstruct note inventory (after Reconstruction complete)."""
    final_note_re = re.compile(r"Final note: pitch=(\d+), start=(\d+), end=(\d+)")
    recon_re = re.compile(r"Reconstruction complete: (\d+) notes")
    snapshots: list[dict[str, object]] = []
    pending: list[dict[str, int]] = []
    last_edit_label = "initial"
    last_edit_line = 0

    edit_patterns: list[tuple[re.Pattern[str], str]] = [
        (
            re.compile(
                r"POSITION EDIT: Note moved from step \d+ to \d+ "
                r"\(tick (\d+) -> (\d+)"
            ),
            "position_edit",
        ),
        (re.compile(r"Note value changed successfully:"), "pitch_change"),
        (re.compile(r"Deleting note"), "delete"),
        (re.compile(r"Created 32nd note"), "insert"),
        (re.compile(r"Applied pitch change overlaps:"), "pitch_overlap"),
        (re.compile(r"LENGTH EDIT:"), "length_edit"),
        (re.compile(r"Edit committed ChangeLength"), "change_length_commit"),
    ]

    for idx, line in enumerate(lines):
        for pattern, kind in edit_patterns:
            match = pattern.search(line)
            if not match:
                continue
            if kind == "position_edit":
                old_tick, new_tick = int(match.group(1)), int(match.group(2))
                if old_tick == new_tick:
                    break
                last_edit_label = f"{kind}:{old_tick}->{new_tick}"
            else:
                last_edit_label = kind
            last_edit_line = idx
            break

        note_match = final_note_re.search(line)
        if note_match:
            pitch, start, end = (int(x) for x in note_match.groups())
            pending.append({"pitch": pitch, "start": start, "end": end})
            continue

        recon_match = recon_re.search(line)
        if recon_match and pending:
            snapshots.append(
                {
                    "line": idx,
                    "edit_label": last_edit_label,
                    "edit_line": last_edit_line,
                    "declared_count": int(recon_match.group(1)),
                    "notes": list(pending),
                    "inventory": _inventory_from_notes(pending, loop_length),
                }
            )
            pending = []

    return snapshots


def _verify_note_length_integrity(lines: list[str]) -> dict[str, object]:
    """Track shortened overlap-note spans and flag failed/partial restore or unexplained loss."""
    loop_length = _parse_loop_length(lines)
    snapshots = _extract_reconstruction_snapshots(lines, loop_length)
    issues: list[str] = []
    deterioration_events: list[dict[str, object]] = []

    # Only analyze after first real edit (record-time reconstructions move bracket only).
    first_edit_idx = next(
        (
            i
            for i, line in enumerate(lines)
            if "POSITION EDIT:" in line and "tick " in line and "->" in line
        ),
        0,
    )
    edit_lines = lines[first_edit_idx:]

    shorten_store_re = re.compile(
        r"Stored original note before shortening: pitch=(\d+), start=(\d+), "
        r"original_end=(\d+), shortened_to=(\d+)"
    )
    shortened_overlap_notes: dict[tuple[int, int], dict[str, int]] = {}
    for line in edit_lines:
        match = shorten_store_re.search(line)
        if match:
            pitch, start, original_end, shortened_to = (int(x) for x in match.groups())
            key = _note_key(pitch, start)
            shortened_overlap_notes[key] = {
                "pitch": pitch,
                "start": start,
                "original_end": original_end,
                "shortened_to": shortened_to,
                "peak_end": original_end,
            }

    if _serial_contains(edit_lines, "Failed to find note-on for shortened note"):
        issues.append("shortened_restore_missing_note_on")

    too_short_re = re.compile(
        r"Will delete note \(too short after shortening\): pitch=(\d+), start=(\d+)"
    )
    for line in edit_lines:
        match = too_short_re.search(line)
        if not match:
            continue
        key = _note_key(int(match.group(1)), int(match.group(2)))
        if key in shortened_overlap_notes:
            _append_edit_verifier_issue(
                issues, f"too_short_delete_on_shortened_overlap_note:p{key[0]}@{key[1]}"
            )
            deterioration_events.append(
                {
                    "kind": "too_short_delete_on_overlap_note",
                    "pitch": key[0],
                    "start": key[1],
                }
            )

    restore_short_re = re.compile(
        r"Restoring shortened note: pitch=(\d+), start=(\d+), "
        r"was shortened to (\d+), restoring to (\d+)"
    )
    recreate_short_re = re.compile(
        r"Recreating shortened (?:overlap note|victim) \(note-on missing\): pitch=(\d+), start=(\d+), end=(\d+)"
    )
    will_restore_re = re.compile(
        r"Will restore note: pitch=(\d+), start=(\d+), end=(\d+)"
    )
    restore_attempted: set[tuple[int, int]] = set()
    for line in edit_lines:
        match = will_restore_re.search(line)
        if match:
            restore_attempted.add(_note_key(int(match.group(1)), int(match.group(2))))

    edit_snapshots = [s for s in snapshots if int(s["line"]) >= first_edit_idx]

    def _snapshot_after(line_idx: int) -> Optional[dict[str, object]]:
        for snap in edit_snapshots:
            if int(snap["line"]) > line_idx:
                return snap
        return edit_snapshots[-1] if edit_snapshots else None

    for idx, line in enumerate(edit_lines):
        global_line = first_edit_idx + idx
        restore_match = restore_short_re.search(line)
        recreate_match = recreate_short_re.search(line)
        if not restore_match and not recreate_match:
            continue
        if restore_match:
            pitch = int(restore_match.group(1))
            start = int(restore_match.group(2))
            target_end = int(restore_match.group(4))
        else:
            assert recreate_match is not None
            pitch = int(recreate_match.group(1))
            start = int(recreate_match.group(2))
            target_end = int(recreate_match.group(3))
        key = _note_key(pitch, start)
        overlap_entry = shortened_overlap_notes.get(key)
        if overlap_entry is None:
            continue
        post = _snapshot_after(global_line)
        if post is None:
            continue
        post_inv = post["inventory"]
        assert isinstance(post_inv, dict)
        if key not in post_inv:
            _append_edit_verifier_issue(
                issues, f"shortened_overlap_note_gone_after_restore:p{pitch}@{start}"
            )
            continue
        restored_end = int(post_inv[key]["end"])
        if restored_end < overlap_entry["original_end"] - 2:
            issues.append(
                f"shortened_not_fully_restored:p{pitch}@{start}:"
                f"end={restored_end}<original={overlap_entry['original_end']}"
            )
            deterioration_events.append(
                {
                    "line": int(post["line"]),
                    "kind": "partial_restore",
                    "pitch": pitch,
                    "start": start,
                    "restored_end": restored_end,
                    "original_end": overlap_entry["original_end"],
                    "target_end": target_end,
                }
            )

    below_peak: list[dict[str, object]] = []
    final_inventory = edit_snapshots[-1]["inventory"] if edit_snapshots else {}
    assert isinstance(final_inventory, dict)
    for key, overlap_entry in shortened_overlap_notes.items():
        original_end = overlap_entry["original_end"]
        if key not in final_inventory:
            if key in restore_attempted:
                _append_edit_verifier_issue(
                    issues, f"shortened_overlap_note_missing_after_restore:p{key[0]}@{key[1]}"
                )
                below_peak.append(
                    {
                        "pitch": key[0],
                        "start": key[1],
                        "original_end": original_end,
                        "final": "missing",
                    }
                )
            continue
        final_end = int(final_inventory[key]["end"])
        if final_end < original_end - 2:
            below_peak.append(
                {
                    "pitch": key[0],
                    "start": key[1],
                    "original_end": original_end,
                    "final_end": final_end,
                }
            )
            if key in restore_attempted:
                issues.append(
                    f"shortened_still_short_after_restore:p{key[0]}@{key[1]}:"
                    f"end={final_end}<original={original_end}"
                )

    return {
        "ok": len(issues) == 0,
        "issues": issues,
        "snapshot_count": len(edit_snapshots),
        "deterioration_events": deterioration_events[:40],
        "below_peak_at_end": below_peak[:30],
        "shortened_overlap_note_count": len(shortened_overlap_notes),
        "loop_length": loop_length,
    }


def _verify_m0_survives_warmup(
    lines: list[str],
    *,
    layout: RecordLayout,
    tick_tolerance: int = 8,
) -> dict[str, object]:
    """M0 must survive warmup and exist before length-edit mode is entered."""
    issues: list[str] = []
    m0_tick = layout.step_to_tick.get(M0_STEP, M0_STEP * TICKS_PER_16TH_STEP)
    delete_re = re.compile(
        rf"Deleting note pitch={M0_PITCH}, start=(\d+), end=(\d+)"
    )
    length_edit_started = False
    early_deletes: list[int] = []
    m0_seen_before_length = False
    final_note_re = re.compile(
        rf"Final note: pitch={M0_PITCH}, start=(\d+), end=(\d+)"
    )
    for line in lines:
        if "NOTE END position (length editing)" in line:
            length_edit_started = True
        if not length_edit_started:
            match = delete_re.search(line)
            if match:
                start = int(match.group(1))
                if abs(start - m0_tick) <= tick_tolerance:
                    early_deletes.append(start)
            note_match = final_note_re.search(line)
            if note_match:
                start = int(note_match.group(1))
                if abs(start - m0_tick) <= tick_tolerance:
                    m0_seen_before_length = True
        elif not m0_seen_before_length:
            note_match = final_note_re.search(line)
            if note_match:
                start = int(note_match.group(1))
                if abs(start - m0_tick) <= tick_tolerance:
                    m0_seen_before_length = True
    if early_deletes:
        issues.append(f"m0_deleted_before_length_edit:start={early_deletes[0]}")
    if length_edit_started and not m0_seen_before_length:
        issues.append(f"m0_missing_before_length_edit:tick={m0_tick}")
    return {
        "ok": len(issues) == 0,
        "issues": issues,
        "m0_tick": m0_tick,
        "early_m0_deletes": early_deletes,
        "m0_seen_before_length_edit": m0_seen_before_length,
    }


def _verify_beat_move_routing(lines: list[str]) -> dict[str, object]:
    """Ensure M0 ±1 beat moves use POSITION EDIT, not stale length-edit routing."""
    issues: list[str] = []
    beat_shuttle_steps = {M0_STEP, M0_STEP + BEAT_MOVE_STEPS}

    if not any(_is_edit_enter_line(line) for line in lines):
        issues.append("missing_edit_session_enter_log")

    for line in lines:
        if "Button released: Ch16 Note38" not in line:
            continue
        dur_match = re.search(r"duration=(\d+)", line)
        if dur_match is None:
            continue
        duration_ms = int(dur_match.group(1))
        if duration_ms <= 5000:
            continue
        line_idx = lines.index(line)
        window = lines[line_idx : line_idx + 6]
        if any("exited edit mode" in w for w in window):
            if not any(_is_edit_enter_line(line) for line in lines[:line_idx]):
                issues.append(f"edit_button_stuck_long_press:duration={duration_ms}")
        break

    shuttle: list[tuple[str, int, int]] = []
    for line in lines:
        pos_match = re.search(
            r"POSITION EDIT: Note moved from step (\d+) to (\d+)", line
        )
        if pos_match:
            from_step = int(pos_match.group(1))
            to_step = int(pos_match.group(2))
            if from_step in beat_shuttle_steps and to_step in beat_shuttle_steps:
                shuttle.append(("position", from_step, to_step))
            continue
        len_match = re.search(
            r"LENGTH EDIT: Note end moved from step (\d+) to (\d+)", line
        )
        if len_match:
            from_step = int(len_match.group(1))
            to_step = int(len_match.group(2))
            if from_step in beat_shuttle_steps and to_step in beat_shuttle_steps:
                shuttle.append(("length", from_step, to_step))
        if len(shuttle) >= 2:
            break

    if len(shuttle) >= 1 and shuttle[0][0] == "length":
        issues.append("beat_forward_routed_as_length_edit")
    if len(shuttle) >= 2 and shuttle[1][0] == "length":
        issues.append("beat_backward_routed_as_length_edit")

    return {
        "ok": len(issues) == 0,
        "issues": issues,
        "shuttle_edits": [{"kind": k, "from": a, "to": b} for k, a, b in shuttle],
    }


def _verify_edit_move_display(
    lines: list[str],
    *,
    tick_tolerance: int = 24,
) -> dict[str, object]:
    """Verify +1 beat, -1 beat, same-pitch overlap, and display note-info updates."""
    issues: list[str] = []
    edits = _parse_position_edits(lines)
    moved = [e for e in _parse_moved_note_events(lines) if e["pitch"] == M0_PITCH]
    dnte = [d for d in _parse_dnte_lines(lines) if d["pitch"] == M0_PITCH]

    beat_forward = any(
        e["delta_step"] == BEAT_MOVE_STEPS
        and e["delta_tick"] >= TICKS_PER_BEAT - tick_tolerance
        for e in edits
    )
    beat_backward = any(
        e["delta_step"] == -BEAT_MOVE_STEPS
        and e["delta_tick"] <= -(TICKS_PER_BEAT - tick_tolerance)
        for e in edits
    )
    same_pitch_overlap = (
        _serial_contains(lines, "Temporarily deleting MIDI event pair")
        or _serial_contains(lines, "Will delete overlapping note")
        or _serial_contains(lines, "Will delete completely contained note")
    )

    def _storage_confirmed_after_move(target_start: int, start_idx: int) -> bool:
        window = lines[start_idx : start_idx + 80]
        for line in window:
            if f",DNTE,{M0_PITCH},{target_start},{target_start}," in line:
                return True
            match = re.search(
                rf"Final note: pitch={M0_PITCH}, start={target_start}, end=\d+", line
            )
            if match:
                return True
        return False

    move_confirmations = 0
    for idx, line in enumerate(lines):
        match = re.search(
            rf"Moved note events: pitch={M0_PITCH} start->(\d+) end->(\d+)", line
        )
        if not match:
            continue
        start_tick = int(match.group(1))
        if _storage_confirmed_after_move(start_tick, idx):
            move_confirmations += 1

    if move_confirmations < 3:
        issues.append(f"move_display_confirmations_low:{move_confirmations}<3")

    if not beat_forward:
        issues.append("missing_beat_forward_move")
    if not beat_backward:
        issues.append("missing_beat_backward_move")
    if not same_pitch_overlap:
        issues.append("missing_same_pitch_overlap")

    if len(moved) < 2:
        issues.append(f"m0_moved_events_low:{len(moved)}")

    dnte_mismatch = [d for d in dnte if d["storage_start"] != d["display_start"]]
    if dnte_mismatch:
        issues.append(f"dnte_storage_display_mismatch:{len(dnte_mismatch)}")

    display_matches_store = move_confirmations >= 3

    return {
        "ok": len(issues) == 0,
        "issues": issues,
        "beat_forward": beat_forward,
        "beat_backward": beat_backward,
        "same_pitch_overlap": same_pitch_overlap,
        "m0_moved_event_count": len(moved),
        "move_display_confirmations": move_confirmations,
        "dnte_count": len(dnte),
        "dnte_samples": dnte[-5:],
        "position_edits": edits[:8],
        "display_matches_store": display_matches_store,
    }


def _fixture_step_tick(layout: RecordLayout, fixture_step: int) -> int:
    return layout.step_to_tick.get(fixture_step, fixture_step * TICKS_PER_16TH_STEP)


def _append_edit_verifier_issue(issues: list[str], issue: str) -> None:
    """OpenSpec issue key; append legacy *_victim_* alias for one release (task 7.3)."""
    issues.append(issue)
    if "_overlap_note_" in issue:
        legacy = issue.replace("_overlap_note_", "_victim_", 1)
        if legacy != issue:
            issues.append(legacy)



_LOOP_END_ACTIVE_RE = re.compile(
    r"Active note at loop end: pitch=(\d+), start=(\d+), end=(\d+)"
)


def _inventory_notes_at_start(
    inventory: dict[tuple[int, int], dict[str, int]],
    *,
    pitch: int,
    start: int,
    tick_tolerance: int,
) -> list[dict[str, int]]:
    return [
        note
        for note in inventory.values()
        if note["pitch"] == pitch and abs(note["start"] - start) <= tick_tolerance
    ]


def _assert_native_lengthen_rematerialize_parity(
    inventory: dict[tuple[int, int], dict[str, int]],
    *,
    layout: RecordLayout,
    loop_length: int,
    m0_pitch: int = M0_PITCH,
    tick_tolerance: int = 24,
    context: str,
) -> list[str]:
    """Mirror native test_edit_apply lengthen + rematerialize assertions on a note inventory.

    - test_lengthen_after_move_keeps_p0_fixture_gate
    - test_change_length_rematerialize_keeps_p0_off_not_loop_end
    """
    issues: list[str] = []
    prefix = f"{context}:" if context else ""
    m0_tick = _fixture_step_tick(layout, M0_STEP)
    p0_tick = _fixture_step_tick(layout, P0_STEP)
    long_end_tick = _fixture_step_tick(layout, LONG_M0_END_FIXTURE_STEP)
    min_gate = RECORD_GATE_TICKS - tick_tolerance
    max_gate = RECORD_GATE_TICKS + tick_tolerance
    min_m0_extended = RECORD_GATE_TICKS + TICKS_PER_16TH_STEP

    m0_notes = _inventory_notes_at_start(
        inventory, pitch=m0_pitch, start=m0_tick, tick_tolerance=tick_tolerance
    )
    p0_notes = _inventory_notes_at_start(
        inventory, pitch=M0_PITCH, start=p0_tick, tick_tolerance=tick_tolerance
    )

    # Native: exactly one M0 note-on pairing at fixture start.
    if len(m0_notes) != 1:
        issues.append(f"{prefix}native_m0_count:{len(m0_notes)}!=1")
    # Native: exactly one P0 note-on at step 12.
    if len(p0_notes) != 1:
        issues.append(f"{prefix}native_p0_count:{len(p0_notes)}!=1")

    if len(m0_notes) == 1:
        m0 = m0_notes[0]
        if m0["length"] < min_m0_extended:
            issues.append(f"{prefix}native_m0_not_lengthened")
        if abs(m0["end"] - long_end_tick) > tick_tolerance:
            issues.append(f"{prefix}native_m0_end_not_at_long_fixture_step")

    if len(p0_notes) == 1:
        p0 = p0_notes[0]
        if not (min_gate <= p0["length"] <= max_gate):
            issues.append(f"{prefix}native_p0_gate_length")
        if p0["end"] >= loop_length - tick_tolerance:
            issues.append(f"{prefix}native_p0_stretched_loop_end")
        # Native: shared release tick with lengthened M0 (two offs @672).
        if len(m0_notes) == 1 and abs(p0["end"] - m0_notes[0]["end"]) > tick_tolerance:
            issues.append(f"{prefix}native_p0_m0_end_tick_mismatch")
        if abs(p0["end"] - long_end_tick) > tick_tolerance:
            issues.append(f"{prefix}native_p0_end_not_at_shared_release")

    return issues


def _assert_native_lengthen_then_pitch_parity(
    inventory: dict[tuple[int, int], dict[str, int]],
    *,
    layout: RecordLayout,
    loop_length: int,
    tick_tolerance: int = 24,
    context: str,
) -> list[str]:
    """Mirror test_change_pitch_on_lengthened_note_keeps_same_pitch_neighbor."""
    issues: list[str] = []
    prefix = f"{context}:" if context else ""
    m0_tick = _fixture_step_tick(layout, M0_STEP)
    p0_tick = _fixture_step_tick(layout, P0_STEP)
    long_end_tick = _fixture_step_tick(layout, LONG_M0_END_FIXTURE_STEP)

    m0_repitch = _inventory_notes_at_start(
        inventory, pitch=A_PITCH, start=m0_tick, tick_tolerance=tick_tolerance
    )
    p0_notes = _inventory_notes_at_start(
        inventory, pitch=M0_PITCH, start=p0_tick, tick_tolerance=tick_tolerance
    )
    stray_m0_lane = _inventory_notes_at_start(
        inventory, pitch=M0_PITCH, start=m0_tick, tick_tolerance=tick_tolerance
    )
    p0_relabeled = _inventory_notes_at_start(
        inventory, pitch=A_PITCH, start=p0_tick, tick_tolerance=tick_tolerance
    )

    if len(m0_repitch) != 1:
        issues.append(f"{prefix}native_m0_pitch_67_count:{len(m0_repitch)}!=1")
    elif abs(m0_repitch[0]["end"] - long_end_tick) > tick_tolerance:
        issues.append(f"{prefix}native_m0_pitch_67_end_mismatch")

    if len(p0_notes) != 1:
        issues.append(f"{prefix}native_p0_pitch_60_count:{len(p0_notes)}!=1")
    elif p0_notes[0]["end"] >= loop_length - tick_tolerance:
        issues.append(f"{prefix}native_p0_loop_end_after_pitch")

    if stray_m0_lane:
        issues.append(f"{prefix}native_stray_pitch_60_at_m0_start")

    if p0_relabeled:
        issues.append(f"{prefix}native_p0_off_relabeled_to_67")

    return issues


def _assert_overlap_pitch_change_mid_move_parity(
    inventory: dict[tuple[int, int], dict[str, int]],
    *,
    layout: RecordLayout,
    loop_length: int,
    tick_tolerance: int = 24,
    context: str,
) -> list[str]:
    """Overlap round-trip pitches M0 while at LONG_OVER_P0_START_STEP (before return home).

    Native test_change_pitch_on_lengthened_note uses home coords; HITL repitches at display
    start — check mover at over-step, not m0_tick.
    """
    issues: list[str] = []
    prefix = f"{context}:" if context else ""
    m0_tick = _fixture_step_tick(layout, M0_STEP)
    over_tick = _fixture_step_tick(layout, LONG_OVER_P0_START_STEP)
    p0_tick = _fixture_step_tick(layout, P0_STEP)
    min_m0_extended = RECORD_GATE_TICKS + TICKS_PER_16TH_STEP

    mover = _inventory_notes_at_start(
        inventory, pitch=A_PITCH, start=over_tick, tick_tolerance=tick_tolerance
    )
    if len(mover) != 1:
        issues.append(f"{prefix}native_m0_pitch_67_at_over_step:{len(mover)}!=1")
    elif mover[0]["length"] < min_m0_extended:
        issues.append(f"{prefix}native_m0_not_lengthened_at_over_step")

    if _inventory_notes_at_start(
        inventory, pitch=M0_PITCH, start=m0_tick, tick_tolerance=tick_tolerance
    ):
        issues.append(f"{prefix}native_stray_pitch_60_at_m0_start")

    p0_notes = _inventory_notes_at_start(
        inventory, pitch=M0_PITCH, start=p0_tick, tick_tolerance=tick_tolerance
    )
    if _inventory_notes_at_start(
        inventory, pitch=A_PITCH, start=p0_tick, tick_tolerance=tick_tolerance
    ):
        issues.append(f"{prefix}native_p0_off_relabeled_to_67")
    if p0_notes and p0_notes[0]["end"] >= loop_length - tick_tolerance:
        issues.append(f"{prefix}native_p0_stretched_loop_end")

    return issues


def _first_line_index_after(lines: list[str], start_idx: int, needle: str) -> int:
    for i in range(max(start_idx, 0), len(lines)):
        if needle in lines[i]:
            return i
    return -1


def _snapshot_position_edit_to_home(
    snapshots: list[dict[str, object]],
    m0_tick: int,
    *,
    tick_tolerance: int = 24,
    min_edit_line: int = -1,
    mover_pitch: int = A_PITCH,
) -> Optional[dict[str, object]]:
    """Last reconstruction after a position edit landing on fixture home (not pre-move scratch)."""
    for snap in reversed(snapshots):
        if min_edit_line >= 0 and int(snap.get("edit_line", -1)) < min_edit_line:
            continue
        label = str(snap.get("edit_label", ""))
        if not label.startswith("position_edit:"):
            continue
        lands_home = label.endswith(f"->{m0_tick}")
        if not lands_home:
            match = re.search(r"->(\d+)$", label)
            lands_home = bool(
                match and abs(int(match.group(1)) - m0_tick) <= tick_tolerance
            )
        if not lands_home:
            continue
        inv = snap.get("inventory", {})
        if isinstance(inv, dict) and _inventory_notes_at_start(
            inv, pitch=mover_pitch, start=m0_tick, tick_tolerance=tick_tolerance
        ):
            return snap
    return None


def _snapshot_overlap_round_trip_home(
    lines: list[str],
    snapshots: list[dict[str, object]],
    loop_length: int,
    *,
    home_line: int,
    m0_tick: int,
    mover_pitch: int = A_PITCH,
    tick_tolerance: int = 24,
) -> Optional[dict[str, object]]:
    """Inventory at overlap round-trip home — after Moved note events, not pre-move scratch recon."""
    if home_line < 0:
        return _snapshot_position_edit_to_home(
            snapshots,
            m0_tick,
            tick_tolerance=tick_tolerance,
            mover_pitch=mover_pitch,
        )

    move_done = -1
    moved_pitch = mover_pitch
    moved_end: Optional[int] = None
    for i in range(home_line, min(home_line + 120, len(lines))):
        moved_match = re.search(
            r"Moved note events: pitch=(\d+) start->(\d+)",
            lines[i],
        )
        if not moved_match:
            continue
        new_start = int(moved_match.group(2))
        if abs(new_start - m0_tick) > tick_tolerance:
            continue
        move_done = i
        moved_pitch = int(moved_match.group(1))
        for j in range(max(home_line, i - 12), i + 1):
            move_line = re.search(
                r"Movement: start \d+->(\d+), end actual (\d+)",
                lines[j],
            )
            if move_line and abs(int(move_line.group(1)) - m0_tick) <= tick_tolerance:
                moved_end = int(move_line.group(2))
                break
        break

    if move_done < 0:
        return _snapshot_position_edit_to_home(
            snapshots,
            m0_tick,
            tick_tolerance=tick_tolerance,
            min_edit_line=home_line,
            mover_pitch=mover_pitch,
        )

    base_snap: Optional[dict[str, object]] = None
    for snap in reversed(snapshots):
        if int(snap["line"]) < home_line:
            base_snap = snap
            break

    notes: list[dict[str, int]] = []
    if base_snap is not None:
        raw_notes = base_snap.get("notes", [])
        assert isinstance(raw_notes, list)
        notes = [dict(n) for n in raw_notes if isinstance(n, dict)]
        min_extended = RECORD_GATE_TICKS + TICKS_PER_16TH_STEP
        notes = [
            n
            for n in notes
            if not (
                n["pitch"] == moved_pitch
                and abs(n["start"] - m0_tick) > tick_tolerance
                and _note_span_length(n["start"], n["end"], loop_length)
                >= min_extended - tick_tolerance
            )
        ]

    applied = False
    for line in lines[move_done : move_done + 12]:
        if ",DNTE," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 8:
            continue
        try:
            pitch = int(parts[3])
            storage = int(parts[4])
            length = int(parts[6])
        except ValueError:
            continue
        if pitch == moved_pitch and abs(storage - m0_tick) <= tick_tolerance:
            end = moved_end if moved_end is not None else storage + length
            notes = [
                n
                for n in notes
                if not (
                    n["pitch"] == pitch
                    and abs(n["start"] - storage) <= tick_tolerance
                )
            ]
            notes.append({"pitch": pitch, "start": storage, "end": end})
            applied = True
            break

    if not applied and moved_end is not None:
        notes = [
            n
            for n in notes
            if not (
                n["pitch"] == moved_pitch
                and abs(n["start"] - m0_tick) <= tick_tolerance
            )
        ]
        notes.append({"pitch": moved_pitch, "start": m0_tick, "end": moved_end})

    if not _inventory_notes_at_start(
        _inventory_from_notes(notes, loop_length),
        pitch=moved_pitch,
        start=m0_tick,
        tick_tolerance=tick_tolerance,
    ):
        return None

    inv = _inventory_from_notes(notes, loop_length)
    return {
        "line": move_done,
        "edit_label": f"position_edit_home:{m0_tick}",
        "edit_line": home_line,
        "declared_count": len(notes),
        "notes": notes,
        "inventory": inv,
    }


def _verify_change_length_store_rebuild(
    lines: list[str],
    *,
    layout: RecordLayout,
    tick_tolerance: int = 24,
) -> dict[str, object]:
    """ChangeLength must commit and rematerialize the edit session store for display.

    Serial parity with native test_edit_apply:
    - test_lengthen_after_move_keeps_p0_fixture_gate
    - test_change_length_rematerialize_keeps_p0_off_not_loop_end
    - test_change_pitch_on_lengthened_note_keeps_same_pitch_neighbor

    Regression target: live lengthen left origEnd==lastEnd so commit was skipped; fader-1
    reselect rebuilt from stale committed replay while the flat cache was discarded — M0 vanished
    from display and P0 (pitch 60 @ step 12) stretched to loop end.
    """
    issues: list[str] = []
    loop_length = _parse_loop_length(lines)
    snapshots = _extract_reconstruction_snapshots(lines, loop_length)
    m0_tick = _fixture_step_tick(layout, M0_STEP)
    p0_tick = _fixture_step_tick(layout, P0_STEP)
    b_tick = _fixture_step_tick(layout, B_STEP)

    change_length_commits = [
        i for i, line in enumerate(lines) if "Edit committed ChangeLength" in line
    ]
    length_disabled_idx = next(
        (i for i, line in enumerate(lines) if "Length editing mode DISABLED" in line),
        -1,
    )
    length_enabled_idx = next(
        (i for i, line in enumerate(lines) if "Length editing mode ENABLED" in line),
        -1,
    )

    if length_enabled_idx < 0:
        issues.append("missing_length_mode_enabled_log")
    if length_disabled_idx < 0:
        issues.append("missing_length_mode_disabled_log")
    if not change_length_commits:
        issues.append("missing_change_length_commit")
    elif length_disabled_idx >= 0:
        first_commit = change_length_commits[0]
        if not (length_disabled_idx - 15 <= first_commit <= length_disabled_idx + 2):
            issues.append("change_length_commit_not_near_length_mode_off")

    def _snapshot_after(line_idx: int) -> Optional[dict[str, object]]:
        for snap in snapshots:
            if int(snap["line"]) > line_idx:
                return snap
        return None

    def _p0_loop_end_orphan(line_idx: int, *, window: int = 80) -> bool:
        for line in lines[line_idx : line_idx + window]:
            match = _LOOP_END_ACTIVE_RE.search(line)
            if not match:
                continue
            pitch, start, _end = (int(x) for x in match.groups())
            if pitch == M0_PITCH and abs(start - p0_tick) <= tick_tolerance:
                return True
        return False

    def _apply_native_checks(
        snap: Optional[dict[str, object]],
        *,
        context: str,
        m0_pitch: int = M0_PITCH,
        include_pitch_parity: bool = False,
        missing_snap_issue: str,
    ) -> None:
        if snap is None:
            issues.append(missing_snap_issue)
            return
        inv = snap.get("inventory", {})
        assert isinstance(inv, dict)
        issues.extend(
            _assert_native_lengthen_rematerialize_parity(
                inv,
                layout=layout,
                loop_length=loop_length,
                m0_pitch=m0_pitch,
                tick_tolerance=tick_tolerance,
                context=context,
            )
        )
        if include_pitch_parity:
            issues.extend(
                _assert_native_lengthen_then_pitch_parity(
                    inv,
                    layout=layout,
                    loop_length=loop_length,
                    tick_tolerance=tick_tolerance,
                    context=context,
                )
            )

    post_commit_snap: Optional[dict[str, object]] = None
    post_pitch_snap: Optional[dict[str, object]] = None
    post_home_snap: Optional[dict[str, object]] = None
    pitch_change_idx = -1
    home_line = _position_edit_line(lines, PAST_HIGHER_NOTE_STEP, M0_STEP)
    if home_line < 0:
        home_line = _position_edit_line(lines, A_STEP, M0_STEP)

    if change_length_commits:
        commit_idx = change_length_commits[0]
        post_commit_snap = _snapshot_after(commit_idx)
        _apply_native_checks(
            post_commit_snap,
            context="after_change_length_commit",
            m0_pitch=M0_PITCH,
            missing_snap_issue="missing_reconstruction_after_change_length_commit",
        )
        if _p0_loop_end_orphan(commit_idx):
            issues.append("p0_active_at_loop_end_after_change_length_commit")

        pitch_change_idx = _first_line_index_after(
            lines, commit_idx, "Note value changed successfully"
        )
        if pitch_change_idx >= 0:
            post_pitch_snap = _snapshot_after(pitch_change_idx)
            if post_pitch_snap is None:
                issues.append("missing_reconstruction_after_overlap_pitch_change")
            else:
                inv = post_pitch_snap.get("inventory", {})
                assert isinstance(inv, dict)
                issues.extend(
                    _assert_overlap_pitch_change_mid_move_parity(
                        inv,
                        layout=layout,
                        loop_length=loop_length,
                        tick_tolerance=tick_tolerance,
                        context="after_overlap_pitch_change",
                    )
                )
            if _p0_loop_end_orphan(pitch_change_idx):
                issues.append("p0_active_at_loop_end_after_overlap_pitch_change")
        else:
            issues.append("missing_overlap_pitch_change_log")

        if home_line >= 0:
            post_home_snap = _snapshot_overlap_round_trip_home(
                lines,
                snapshots,
                loop_length,
                home_line=home_line,
                m0_tick=m0_tick,
                mover_pitch=A_PITCH,
                tick_tolerance=tick_tolerance,
            )
            _apply_native_checks(
                post_home_snap,
                context="after_overlap_round_trip_home",
                m0_pitch=A_PITCH,
                include_pitch_parity=True,
                missing_snap_issue="missing_reconstruction_after_overlap_round_trip_home",
            )
            if _p0_loop_end_orphan(home_line):
                issues.append("p0_active_at_loop_end_after_overlap_round_trip_home")

    # Reselect B after overlap round-trip home: rematerialize on commit must keep display notes.
    b_select_idx = -1
    if home_line >= 0:
        for i in range(home_line, len(lines)):
            line = lines[i]
            if "Select fader: selected note" not in line:
                continue
            match = re.search(r"selected note \d+ at tick (\d+)", line)
            if match and abs(int(match.group(1)) - b_tick) <= tick_tolerance:
                b_select_idx = i
                break

    post_reselect_b_snap: Optional[dict[str, object]] = None
    if b_select_idx >= 0:
        post_reselect_b_snap = _snapshot_after(b_select_idx)
        _apply_native_checks(
            post_reselect_b_snap,
            context="after_reselect_b",
            m0_pitch=A_PITCH,
            include_pitch_parity=True,
            missing_snap_issue="missing_reconstruction_after_reselect_b",
        )
        if _p0_loop_end_orphan(b_select_idx):
            issues.append("p0_active_at_loop_end_after_reselect_b")

    return {
        "ok": len(issues) == 0,
        "issues": issues,
        "change_length_commit_count": len(change_length_commits),
        "length_mode_disabled_line": length_disabled_idx,
        "post_commit_snapshot_line": (
            int(post_commit_snap["line"]) if post_commit_snap is not None else None
        ),
        "overlap_pitch_change_line": pitch_change_idx,
        "post_pitch_snapshot_line": (
            int(post_pitch_snap["line"]) if post_pitch_snap is not None else None
        ),
        "overlap_round_trip_home_line": home_line,
        "post_home_snapshot_line": (
            int(post_home_snap["line"]) if post_home_snap is not None else None
        ),
        "reselect_b_line": b_select_idx,
        "post_reselect_b_snapshot_line": (
            int(post_reselect_b_snap["line"]) if post_reselect_b_snap is not None else None
        ),
        "m0_tick": m0_tick,
        "p0_tick": p0_tick,
        "b_tick": b_tick,
    }


def _verify_delay_move_insert_reorder(
    lines: list[str],
    *,
    layout: RecordLayout,
    tick_tolerance: int = 24,
) -> dict[str, object]:
    """D delay move (64@18 onto 67@8, pitch merge) + delete/insert/move-over-new-note."""
    issues: list[str] = []
    a_tick = _fixture_step_tick(layout, A_STEP)
    insert_tick = _fixture_step_tick(layout, INSERT_NOTE_STEP)

    d_moves = [e for e in _parse_moved_note_events(lines) if e["pitch"] == D_PITCH]
    d_delay_to_a = any(abs(e["start"] - a_tick) <= tick_tolerance for e in d_moves)

    pitch_merge = _serial_contains(lines, "currentNote=64 newNote=67") or _serial_contains(
        lines, "Will delete completely contained note"
    ) or _serial_contains(lines, "Will delete overlapping note")

    created_count = _count_serial_substrings(lines, "Created 32nd note")
    delete_count = _count_serial_substrings(lines, "Deleting note")
    create_blocked = _serial_contains(lines, "create ignored (note at bracket)")

    edits = _parse_position_edits(lines)
    move_over_insert = any(
        e["to_step"] == INSERT_NOTE_STEP
        and abs(e["new_rel"] - insert_tick) <= tick_tolerance
        for e in edits
    )

    dnte_at_insert = any(
        abs(d["storage_start"] - insert_tick) <= tick_tolerance for d in _parse_dnte_lines(lines)
    )

    move_back_off_insert = any(
        e["from_step"] == INSERT_NOTE_STEP and e["to_step"] == M0_POST_SANDWICH_STEP
        for e in edits
    )
    move_over_line = _position_edit_line(lines, M0_POST_SANDWICH_STEP, INSERT_NOTE_STEP)
    move_back_line = _position_edit_line(lines, INSERT_NOTE_STEP, M0_POST_SANDWICH_STEP)
    insert_deleted_on_move_over = False
    insert_mover_same_pitch = False
    insert_restored_after_move_back = False
    if move_over_line >= 0:
        over_window = lines[move_over_line : move_over_line + 80]
        insert_deleted_on_move_over = any(
            _insert_overlap_delete_line(line, insert_tick=insert_tick, tick_tolerance=tick_tolerance)
            for line in over_window
        )
        insert_mover_same_pitch = any(_insert_mover_pitch_line(line) for line in over_window[:25])
    if move_back_line >= 0:
        back_window = lines[move_back_line : move_back_line + 120]
        insert_restored_after_move_back = any(
            "Restoring deleted note" in line
            and f"pitch={M0_PITCH}" in line
            and _line_has_start_near(line, insert_tick, tick_tolerance)
            for line in back_window
        ) or any(
            "Will restore note" in line
            and f"pitch={M0_PITCH}" in line
            and _line_has_start_near(line, insert_tick, tick_tolerance)
            for line in back_window
        ) or any(
            "Restoring hidden overlap note" in line
            and f"pitch={M0_PITCH}" in line
            and _line_has_start_near(line, insert_tick, tick_tolerance)
            for line in back_window
        )
        if not insert_restored_after_move_back:
            for line in back_window:
                match = re.search(
                    rf"Final note: pitch={M0_PITCH}, start=(\d+), end=(\d+)", line
                )
                if not match:
                    continue
                if abs(int(match.group(1)) - insert_tick) <= tick_tolerance:
                    span = int(match.group(2)) - int(match.group(1))
                    if 12 <= span <= 36:
                        insert_restored_after_move_back = True
                        break

    insert_present_after_in_edit_redo = False
    pre_undo_insert_selected: Optional[bool] = None
    create_lines = [
        i for i, line in enumerate(lines) if "EditSelectNoteState: Created 32nd note" in line
    ]
    if create_lines:
        last_create = create_lines[-1]
        undo_line = next(
            (
                i
                for i, line in enumerate(lines[last_create:], start=last_create)
                if "EditSession undo" in line or "Overdub undone" in line
            ),
            -1,
        )
        redo_line = next(
            (
                i
                for i, line in enumerate(lines[last_create:], start=last_create)
                if "EditSession redo" in line or "Overdub redone" in line
            ),
            -1,
        )
        if undo_line >= 0 and redo_line > undo_line:
            pre_undo_select_lines = [
                line
                for line in lines[last_create:undo_line]
                if "Select fader:" in line and f"tick {insert_tick}" in line
            ]
            if pre_undo_select_lines:
                pre_undo_last = pre_undo_select_lines[-1]
                if "selected note" in pre_undo_last:
                    pre_undo_insert_selected = True
                elif "selected empty step" in pre_undo_last:
                    pre_undo_insert_selected = False
            exit_line = next(
                (
                    i
                    for i, line in enumerate(lines[redo_line:], start=redo_line)
                    if "exited edit mode" in line
                ),
                -1,
            )
            window_end = (
                exit_line if exit_line > redo_line else min(len(lines), redo_line + 800)
            )
            redo_window = lines[redo_line:window_end]
            select_probe_lines = [
                line
                for line in redo_window
                if "Select fader:" in line and f"tick {insert_tick}" in line
            ]
            insert_selected_after_in_edit_redo = any(
                "selected note" in line for line in select_probe_lines
            )
            insert_empty_after_in_edit_redo = any(
                "selected empty step" in line for line in select_probe_lines
            )
            insert_present_after_in_edit_redo = any(
                (m := re.search(
                    rf"Final note: pitch={M0_PITCH}, start=(\d+), end=(\d+)", line
                ))
                and abs(int(m.group(1)) - insert_tick) <= tick_tolerance
                and 12 <= int(m.group(2)) - int(m.group(1)) <= 36
                for line in redo_window
            )
            if pre_undo_insert_selected is True:
                insert_present_after_in_edit_redo = (
                    insert_present_after_in_edit_redo or insert_selected_after_in_edit_redo
                )
            elif pre_undo_insert_selected is False:
                insert_present_after_in_edit_redo = insert_empty_after_in_edit_redo
            else:
                insert_present_after_in_edit_redo = (
                    insert_present_after_in_edit_redo or insert_selected_after_in_edit_redo
                )

    if not d_delay_to_a:
        issues.append("missing_d_delay_move_to_a")
    if not pitch_merge:
        issues.append("missing_pitch_merge_64_to_67")
    if created_count < 1:
        issues.append(f"created_32nd_low:{created_count}<1")
    if create_blocked:
        issues.append("insert_create_blocked_at_bracket")
    if delete_count < 2:
        issues.append(f"delete_log_low:{delete_count}<2")
    if not move_over_insert:
        issues.append("missing_move_over_inserted_note")
    elif not insert_mover_same_pitch:
        issues.append("insert_mover_not_pitch_60")
    elif not insert_deleted_on_move_over:
        issues.append("insert_not_deleted_on_move_over")
    if not dnte_at_insert:
        issues.append("missing_dnte_at_insert_step")
    if not move_back_off_insert:
        issues.append("missing_move_back_off_inserted_note")
    elif not insert_deleted_on_move_over:
        issues.append("insert_restore_skipped_no_prior_delete")
    elif not insert_restored_after_move_back:
        issues.append("insert_not_restored_after_move_back")
    if not insert_present_after_in_edit_redo:
        issues.append("insert_missing_after_in_edit_redo")

    return {
        "ok": len(issues) == 0,
        "issues": issues,
        "d_delay_move_to_a": d_delay_to_a,
        "pitch_merge_64_to_67": pitch_merge,
        "created_32nd_count": created_count,
        "delete_log_count": delete_count,
        "move_over_inserted": move_over_insert,
        "insert_mover_same_pitch": insert_mover_same_pitch,
        "insert_deleted_on_move_over": insert_deleted_on_move_over,
        "dnte_at_insert_step": dnte_at_insert,
        "move_back_off_insert": move_back_off_insert,
        "insert_restored_after_move_back": insert_restored_after_move_back,
        "insert_present_after_in_edit_redo": insert_present_after_in_edit_redo,
        "pre_undo_insert_selected": pre_undo_insert_selected,
    }


def _last_revt_note_on_pairs(lines: list[str]) -> list[tuple[int, int]]:
    """Latest REVT snapshot in the capture: (tick, pitch) note-on pairs."""
    by_ts: dict[int, list[tuple[int, int]]] = {}
    for line in lines:
        if ",REVT," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 6:
            continue
        try:
            ts = int(parts[1])
            tick = int(parts[3])
            pitch = int(parts[5])
        except ValueError:
            continue
        by_ts.setdefault(ts, []).append((tick, pitch))
    if not by_ts:
        return []
    return by_ts[max(by_ts)]


def _line_has_start_near(line: str, tick: int, tolerance: int) -> bool:
    m = re.search(r"start=(\d+)", line)
    if not m:
        return False
    return abs(int(m.group(1)) - tick) <= tolerance


def _position_edit_line(lines: list[str], from_step: int, to_step: int) -> int:
    needle = f"POSITION EDIT: Note moved from step {from_step} to {to_step}"
    for i, line in enumerate(lines):
        if needle in line:
            return i
    return -1


def _inventory_note_near(
    inventory: dict[tuple[int, int], dict[str, int]],
    *,
    pitch: int,
    start: int,
    tick_tolerance: int,
) -> Optional[dict[str, int]]:
    for note in inventory.values():
        if note["pitch"] == pitch and abs(note["start"] - start) <= tick_tolerance:
            return note
    return None


def _last_snapshot_after(
    snapshots: list[dict[str, object]], needle: str
) -> Optional[dict[str, object]]:
    last: Optional[dict[str, object]] = None
    for snap in snapshots:
        label = str(snap.get("edit_label", ""))
        if needle in label or needle in str(snap.get("edit_line", "")):
            last = snap
    return last


def _serial_window_has_select_fader(lines: list[str], start: int, end: int) -> bool:
    """True only for incoming user fader-1 selection, not outbound select-note sync."""
    for line in lines[start:end]:
        if "Accepting fader 1 input" in line:
            return True
        if "Received pitchbend: ch=16" in line:
            return True
    return False


def _verify_long_over_short_pitch_restore(
    lines: list[str],
    *,
    layout: RecordLayout,
    tick_tolerance: int = 24,
) -> dict[str, object]:
    """Long M0 over P0, pitch lane change, move past A, return home; short B over long M0 round-trip."""
    issues: list[str] = []
    loop_length = _parse_loop_length(lines)
    snapshots = _extract_reconstruction_snapshots(lines, loop_length)
    m0_tick = _fixture_step_tick(layout, M0_STEP)
    b_tick = _fixture_step_tick(layout, B_STEP)
    a_tick = _fixture_step_tick(layout, A_STEP)
    p0_tick = _fixture_step_tick(layout, P0_STEP)
    past_a_tick = _fixture_step_tick(layout, PAST_HIGHER_NOTE_STEP)
    long_over_start_tick = _fixture_step_tick(layout, LONG_OVER_P0_START_STEP)

    contained_delete = any(
        "Will delete completely contained note" in line and f"pitch={M0_PITCH}" in line
        for line in lines
    )
    pitch_restore_log = any("Will restore note after pitch change" in line for line in lines) or any(
        "Restored" in line and "after pitch change" in line for line in lines
    )
    p0_restore_event = any(
        f"Restoring deleted note: pitch={M0_PITCH}, start={p0_tick}," in line for line in lines
    ) or any(
        re.search(rf"Final note: pitch={M0_PITCH}, start={p0_tick}, end=\d+", line)
        for line in lines
    )
    restore_fail = _serial_contains(lines, "Failed to find note-on for shortened note")

    round_trip_home = any(
        re.search(
            rf"POSITION EDIT: Note moved from step \d+ to {M0_STEP} "
            rf"\(tick \d+ -> {m0_tick},",
            line,
        )
        for line in lines
    )
    short_over_long_forward = False
    short_over_long_restore = False
    cross_pitch_no_shorten_on_long_m0 = True
    b_fwd_line = _position_edit_line(lines, B_STEP, A_STEP)
    b_back_line = _position_edit_line(lines, A_STEP, B_STEP)
    if b_fwd_line >= 0 and b_back_line > b_fwd_line:
        cross_pitch_no_shorten_on_long_m0 = not any(
            (
                "Stored original note before shortening" in line
                or "Will shorten note (short-over-long)" in line
            )
            and f"pitch={A_PITCH}" in line
            and _line_has_start_near(line, m0_tick, tick_tolerance)
            for line in lines[b_fwd_line:b_back_line]
        )
        # Same-pitch short-over-long only (cross-pitch must not shorten the long mover lane).
        short_over_long_forward = any(
            "Will shorten note" in line
            and f"pitch={A_PITCH}" in line
            and _line_has_start_near(line, m0_tick, tick_tolerance)
            and "short-over-long" not in line
            for line in lines[b_fwd_line:b_back_line]
        )
        short_over_long_restore = any(
            "Restoring shortened note" in line
            and f"pitch={A_PITCH}" in line
            and _line_has_start_near(line, m0_tick, tick_tolerance)
            for line in lines[b_back_line : b_back_line + 400]
        )

    # No fader-1 reselect between move-over-P0 and pitch change (continuous edit session).
    move_over_line = _position_edit_line(lines, M0_STEP, LONG_OVER_P0_START_STEP)
    inner_a_after_move_over = False
    if move_over_line >= 0:
        for line in lines[move_over_line : move_over_line + 120]:
            if re.search(rf"Final note: pitch={A_PITCH}, start={a_tick}, end=\d+", line):
                inner_a_after_move_over = True
                break
    pitch_idx = -1
    if move_over_line >= 0:
        pitch_idx = next(
            (
                i
                for i, line in enumerate(lines)
                if i > move_over_line and "Note value changed successfully:" in line
            ),
            -1,
        )
    no_reselect_before_pitch = True
    if move_over_line >= 0 and pitch_idx > move_over_line:
        no_reselect_before_pitch = not _serial_window_has_select_fader(
            lines, move_over_line, pitch_idx
        )
    past_a_line = _position_edit_line(lines, LONG_OVER_P0_START_STEP, PAST_HIGHER_NOTE_STEP)
    if past_a_line < 0:
        past_a_line = _position_edit_line(lines, A_STEP, PAST_HIGHER_NOTE_STEP)
    home_line = _position_edit_line(lines, PAST_HIGHER_NOTE_STEP, M0_STEP)
    if home_line < 0:
        home_line = _position_edit_line(lines, A_STEP, M0_STEP)

    inner_a_after_pitch = False
    inner_a_pitch_merge_log = False
    max_gate = RECORD_GATE_TICKS + tick_tolerance
    pitch_window_start = move_over_line if move_over_line >= 0 else max(pitch_idx - 40, 0)
    pitch_window_end = pitch_idx + 80 if pitch_idx >= 0 else pitch_window_start + 120
    if pitch_idx >= 0:
        pitch_window = lines[pitch_window_start:pitch_window_end]
        inner_a_pitch_merge_log = any(
            "Merged" in line
            and             (
                "adjacent same-pitch notes into moving note range" in line
                or "adjacent same-pitch notes into footprint" in line
                or "adjacent same-pitch notes into span" in line
            )
            for line in pitch_window
        )
        post_pitch_window = lines[pitch_idx : pitch_idx + 80]
        for line in post_pitch_window:
            m = re.search(rf"Final note: pitch={A_PITCH}, start={a_tick}, end=(\d+)", line)
            if not m:
                continue
            end_tick = int(m.group(1))
            span = end_tick - a_tick
            if 0 < span <= max_gate:
                inner_a_after_pitch = True
                break
        if inner_a_pitch_merge_log:
            inner_a_after_pitch = False

    inner_a_visible_after_move_past = False
    if past_a_line >= 0:
        for snap in snapshots:
            if snap.get("edit_line", -1) == past_a_line or (
                isinstance(snap.get("edit_label"), str)
                and snap["edit_label"].endswith(f"->{past_a_tick}")
            ):
                inv = snap.get("inventory", {})
                if isinstance(inv, dict) and _inventory_note_near(
                    inv, pitch=A_PITCH, start=a_tick, tick_tolerance=tick_tolerance
                ):
                    inner_a_visible_after_move_past = True
                    break
        if not inner_a_visible_after_move_past:
            for line in lines[past_a_line : past_a_line + 120]:
                if re.search(rf"Final note: pitch={A_PITCH}, start={a_tick}, end=\d+", line):
                    inner_a_visible_after_move_past = True
                    break

    inner_a_contained_at_home = False
    if home_line >= 0:
        inner_a_contained_at_home = any(
            (
                "Will delete completely contained note" in line
                or "Cannot restore note" in line
                or "Stored deleted note" in line
            )
            and f"pitch={A_PITCH}" in line
            and _line_has_start_near(line, a_tick, tick_tolerance)
            for line in lines[home_line : home_line + 120]
        )

    revt_pairs = _last_revt_note_on_pairs(lines)
    p0_in_revt = any(
        pitch == M0_PITCH and abs(tick - p0_tick) <= tick_tolerance for tick, pitch in revt_pairs
    )
    p0_in_store = p0_in_revt or (pitch_restore_log and p0_restore_event)

    visible_after_past_a = inner_a_visible_after_move_past
    if not visible_after_past_a and past_a_line >= 0:
        for d in reversed(_parse_dnte_lines(lines[past_a_line : past_a_line + 200])):
            if d["pitch"] != A_PITCH:
                continue
            if abs(d["storage_start"] - a_tick) <= tick_tolerance:
                visible_after_past_a = True
                break

    home_snap = _snapshot_overlap_round_trip_home(
        lines,
        snapshots,
        loop_length,
        home_line=home_line,
        m0_tick=m0_tick,
        mover_pitch=A_PITCH,
        tick_tolerance=tick_tolerance,
    )
    inner_b_ok = False
    inner_a_ok = False
    inner_p0_ok = False
    m0_home_ok = False
    if home_snap is not None:
        inv = home_snap["inventory"]
        assert isinstance(inv, dict)
        b_note = _inventory_note_near(
            inv, pitch=B_PITCH, start=b_tick, tick_tolerance=tick_tolerance
        )
        a_note = _inventory_note_near(
            inv, pitch=A_PITCH, start=a_tick, tick_tolerance=tick_tolerance
        )
        p0_note = _inventory_note_near(
            inv, pitch=M0_PITCH, start=p0_tick, tick_tolerance=tick_tolerance
        )
        m0_note = _inventory_note_near(
            inv, pitch=A_PITCH, start=m0_tick, tick_tolerance=tick_tolerance
        )
        min_gate = RECORD_GATE_TICKS - tick_tolerance
        if b_note is not None and b_note["length"] >= min_gate:
            inner_b_ok = True
        if a_note is not None and a_note["length"] >= min_gate:
            inner_a_ok = True
        if p0_note is not None and p0_note["length"] >= min_gate:
            inner_p0_ok = True
        if m0_note is not None and m0_note["length"] >= min_gate:
            m0_home_ok = True

    if not contained_delete:
        issues.append("missing_contained_delete_long_over_short")
    if not pitch_restore_log:
        issues.append("missing_pitch_restore_log")
    if not p0_restore_event:
        issues.append("missing_p0_restore_after_pitch")
    if not p0_in_store:
        issues.append("missing_p0_revt_after_pitch")
    if restore_fail:
        issues.append("restore_fail_note_on_missing")
    if not visible_after_past_a:
        issues.append("selected_note_not_visible_after_move_past_higher")
    if not round_trip_home:
        issues.append("missing_round_trip_home_position_edit")
    if not no_reselect_before_pitch:
        issues.append("fader1_reselect_between_move_and_pitch")
    if not inner_a_after_pitch:
        issues.append("inner_a_merged_or_lost_at_pitch_change")
    if inner_a_pitch_merge_log:
        issues.append("inner_a_adjacent_merge_log_at_pitch_change")
    if not inner_a_visible_after_move_past:
        issues.append("inner_a_not_visible_after_move_past")
    if round_trip_home and not inner_a_ok:
        issues.append("inner_a_missing_after_round_trip")
    if not cross_pitch_no_shorten_on_long_m0:
        issues.append("cross_pitch_shortened_long_m0")
    if round_trip_home and not inner_b_ok:
        issues.append("inner_b_missing_after_round_trip")
    if move_over_line >= 0 and not inner_a_after_move_over:
        issues.append("inner_a_missing_after_move_over")
    if round_trip_home and not inner_p0_ok:
        issues.append("inner_p0_missing_after_round_trip")
    if round_trip_home and not m0_home_ok:
        issues.append("m0_not_home_after_round_trip")

    return {
        "ok": len(issues) == 0,
        "issues": issues,
        "contained_delete": contained_delete,
        "pitch_restore_log": pitch_restore_log,
        "p0_restore_event": p0_restore_event,
        "p0_in_revt": p0_in_revt,
        "p0_in_store": p0_in_store,
        "restore_fail_note_on": restore_fail,
        "visible_after_move_past_higher": visible_after_past_a,
        "round_trip_home": round_trip_home,
        "no_reselect_before_pitch": no_reselect_before_pitch,
        "short_over_long_forward": short_over_long_forward,
        "short_over_long_restore": short_over_long_restore,
        "cross_pitch_no_shorten_on_long_m0": cross_pitch_no_shorten_on_long_m0,
        "inner_b_ok": inner_b_ok,
        "inner_a_after_pitch": inner_a_after_pitch,
        "inner_a_pitch_merge_log": inner_a_pitch_merge_log,
        "inner_a_visible_after_move_past": inner_a_visible_after_move_past,
        "inner_a_contained_at_home": inner_a_contained_at_home,
        "inner_a_after_move_over": inner_a_after_move_over,
        "inner_p0_ok": inner_p0_ok,
        "m0_home_ok": m0_home_ok,
        "p0_tick": p0_tick,
        "past_a_tick": past_a_tick,
        "m0_tick": m0_tick,
    }


def _verify_split_overlap_note_round_trip(
    lines: list[str],
    *,
    layout: RecordLayout,
    tick_tolerance: int = 24,
) -> dict[str, object]:
    """Distinct AC: M0 session pitch+position without reselect — inner A survives and returns home."""
    primary_issues: list[str] = []
    loop_length = _parse_loop_length(lines)
    snapshots = _extract_reconstruction_snapshots(lines, loop_length)
    m0_tick = _fixture_step_tick(layout, M0_STEP)
    a_tick = _fixture_step_tick(layout, A_STEP)
    min_gate = RECORD_GATE_TICKS - tick_tolerance
    max_gate = RECORD_GATE_TICKS + tick_tolerance

    move_over_line = _position_edit_line(lines, M0_STEP, LONG_OVER_P0_START_STEP)
    pitch_idx = -1
    if move_over_line >= 0:
        pitch_idx = next(
            (
                i
                for i, line in enumerate(lines)
                if i > move_over_line and "Note value changed successfully:" in line
            ),
            -1,
        )
    past_a_line = _position_edit_line(lines, LONG_OVER_P0_START_STEP, PAST_HIGHER_NOTE_STEP)
    if past_a_line < 0:
        past_a_line = _position_edit_line(lines, A_STEP, PAST_HIGHER_NOTE_STEP)
    home_line = _position_edit_line(lines, PAST_HIGHER_NOTE_STEP, M0_STEP)
    if home_line < 0:
        home_line = _position_edit_line(lines, A_STEP, M0_STEP)

    inner_a_after_pitch = False
    inner_a_pitch_merge_log = False
    if pitch_idx >= 0:
        pitch_window_start = move_over_line if move_over_line >= 0 else max(pitch_idx - 40, 0)
        pitch_window = lines[pitch_window_start : pitch_idx + 80]
        inner_a_pitch_merge_log = any(
            "Merged" in line
            and             (
                "adjacent same-pitch notes into moving note range" in line
                or "adjacent same-pitch notes into footprint" in line
                or "adjacent same-pitch notes into span" in line
            )
            for line in pitch_window
        )
        for line in lines[pitch_idx : pitch_idx + 80]:
            m = re.search(rf"Final note: pitch={A_PITCH}, start={a_tick}, end=(\d+)", line)
            if not m:
                continue
            span = int(m.group(1)) - a_tick
            if 0 < span <= max_gate:
                inner_a_after_pitch = True
                break
        if inner_a_pitch_merge_log:
            inner_a_after_pitch = False

    inner_a_visible_after_move_past = False
    if past_a_line >= 0:
        for line in lines[past_a_line : past_a_line + 120]:
            if re.search(rf"Final note: pitch={A_PITCH}, start={a_tick}, end=\d+", line):
                inner_a_visible_after_move_past = True
                break

    inner_a_recaptured_at_home = False
    home_snap = _snapshot_overlap_round_trip_home(
        lines,
        snapshots,
        loop_length,
        home_line=home_line,
        m0_tick=m0_tick,
        mover_pitch=A_PITCH,
        tick_tolerance=tick_tolerance,
    )
    mover_at_home = False
    if home_snap is not None:
        inv = home_snap.get("inventory", {})
        assert isinstance(inv, dict)
        a_note = _inventory_note_near(
            inv, pitch=A_PITCH, start=a_tick, tick_tolerance=tick_tolerance
        )
        m0_note = _inventory_note_near(
            inv, pitch=A_PITCH, start=m0_tick, tick_tolerance=tick_tolerance
        )
        if a_note is not None and a_note["length"] >= min_gate:
            inner_a_recaptured_at_home = True
        if m0_note is not None and m0_note["length"] > max_gate:
            mover_at_home = True

    cross_pitch_no_shorten = True
    b_fwd_line = _position_edit_line(lines, B_STEP, A_STEP)
    b_back_line = _position_edit_line(lines, A_STEP, B_STEP)
    if b_fwd_line >= 0 and b_back_line > b_fwd_line:
        cross_pitch_no_shorten = not any(
            (
                "Stored original note before shortening" in line
                or "Will shorten note (short-over-long)" in line
            )
            and f"pitch={A_PITCH}" in line
            and _line_has_start_near(line, m0_tick, tick_tolerance)
            for line in lines[b_fwd_line:b_back_line]
        )

    if not inner_a_after_pitch:
        primary_issues.append("split_overlap_note_inner_a_lost_at_pitch")
    if inner_a_pitch_merge_log:
        primary_issues.append("split_overlap_note_inner_a_merged_at_pitch")
    if not inner_a_visible_after_move_past:
        primary_issues.append("split_overlap_note_inner_a_missing_after_move_past")
    if not inner_a_recaptured_at_home:
        primary_issues.append("split_overlap_note_inner_a_missing_at_home")
    if not mover_at_home:
        primary_issues.append("split_overlap_note_mover_not_home")
    if not cross_pitch_no_shorten:
        primary_issues.append("split_overlap_note_cross_pitch_shortened_long_m0")

    issues: list[str] = []
    for issue in primary_issues:
        _append_edit_verifier_issue(issues, issue)

    return {
        "ok": len(primary_issues) == 0,
        "issues": issues,
        "primary_issues": primary_issues,
        "inner_a_after_pitch": inner_a_after_pitch,
        "inner_a_visible_after_move_past": inner_a_visible_after_move_past,
        "inner_a_recaptured_at_home": inner_a_recaptured_at_home,
        "mover_at_home": mover_at_home,
        "cross_pitch_no_shorten_on_long_m0": cross_pitch_no_shorten,
        "a_tick": a_tick,
        "m0_tick": m0_tick,
    }


def _verify_split_victim_round_trip(
    lines: list[str],
    *,
    layout: RecordLayout,
    tick_tolerance: int = 24,
) -> dict[str, object]:
    """Legacy alias — prefer _verify_split_overlap_note_round_trip."""
    return _verify_split_overlap_note_round_trip(
        lines, layout=layout, tick_tolerance=tick_tolerance
    )


def _count_note_edit_pass_undo_logs(lines: list[str]) -> int:
    return (
        _count_serial_substrings(lines, "EditSession undo")
        + _count_serial_substrings_any(lines, _LEGACY_EDIT_PASS_UNDONE_MARKERS)
        + _count_serial_substrings(lines, "Overdub undone")
    )


def _count_note_edit_pass_redo_logs(lines: list[str]) -> int:
    return (
        _count_serial_substrings(lines, "EditSession redo")
        + _count_serial_substrings_any(lines, _LEGACY_EDIT_PASS_REDONE_MARKERS)
        + _count_serial_substrings(lines, "Overdub redone")
    )


def _verify_m8_edit_pass_commit(lines: list[str]) -> dict[str, object]:
    """Exactly one note-edit pass close, with replacement after in-edit undo/redo."""
    enter_idx: Optional[int] = None
    exit_idx: Optional[int] = None
    for i, line in enumerate(lines):
        if enter_idx is None and _is_edit_enter_line(line):
            enter_idx = i
        if enter_idx is not None and "exited edit mode" in line:
            exit_idx = i
            break
    issues: list[str] = []
    if enter_idx is None or exit_idx is None:
        issues.append("m8_pass:enter_or_exit_missing")
        return {"ok": False, "committed_count": 0, "issues": issues}
    # Pass close is logged on exit (after "exited edit mode" line) — include a short tail.
    window_end = min(len(lines), exit_idx + 9)
    window = lines[enter_idx:window_end]
    committed_count = sum(
        1
        for w in window
        if "NoteEditPassClosed" in w or "NoteEditSessionCommitted" in w
    )
    in_edit_undo_redo = any("EditSession undo" in w for w in window) and any(
        "EditSession redo" in w for w in window
    )
    replacement_count = sum(1 for w in window if "NoteEditPass replaced" in w)
    if committed_count != 1:
        issues.append(f"m8_pass:committed_count:{committed_count}!=1")
    if in_edit_undo_redo and replacement_count < 1:
        issues.append("m8_pass:replacement_missing_after_in_edit_undo_redo")
    return {
        "ok": not issues,
        "committed_count": committed_count,
        "replacement_count": replacement_count,
        "issues": issues,
    }


def _verify_edit_serial(
    lines: list[str],
    *,
    expected_markers: list[str],
    min_revt_count: int,
    min_undo_logs: int = 0,
    min_redo_logs: int = 0,
    verify_move_display: bool = True,
    verify_long_over_short_pitch: bool = True,
    verify_note_lengths: bool = True,
    verify_m8_edit_pass: bool = True,
    record_layout: Optional[RecordLayout] = None,
) -> dict[str, object]:
    revt_ticks = _extract_revt_note_on_ticks(lines)
    issues: list[str] = []
    markers_found = {m: _serial_marker_found(lines, m) for m in expected_markers}
    for m, found in markers_found.items():
        if not found:
            issues.append(f"missing_marker:{m}")
    if len(revt_ticks) < min_revt_count:
        issues.append(f"revt_count_low:{len(revt_ticks)}<{min_revt_count}")
    undo_log_count = _count_note_edit_pass_undo_logs(lines)
    redo_log_count = _count_note_edit_pass_redo_logs(lines)
    if undo_log_count < min_undo_logs:
        issues.append(f"undo_log_low:{undo_log_count}<{min_undo_logs}")
    if redo_log_count < min_redo_logs:
        issues.append(f"redo_log_low:{redo_log_count}<{min_redo_logs}")

    session_state_enter: Optional[dict[str, object]] = None
    session_undo_routing: Optional[dict[str, object]] = None
    warmup_empty_nav_create: Optional[dict[str, object]] = None
    session_state_enter = _verify_session_state_enter(lines)
    if not session_state_enter.get("ok"):
        issues.extend(session_state_enter.get("issues", []))
    session_undo_routing = _verify_session_undo_redo_routing(lines)
    if not session_undo_routing.get("ok"):
        issues.extend(session_undo_routing.get("issues", []))
    warmup_empty_nav_create = _verify_warmup_empty_nav_create(lines)
    if not warmup_empty_nav_create.get("ok"):
        issues.extend(warmup_empty_nav_create.get("issues", []))

    move_display: Optional[dict[str, object]] = None
    beat_move_routing: Optional[dict[str, object]] = None
    m0_warmup: Optional[dict[str, object]] = None
    if verify_move_display:
        beat_move_routing = _verify_beat_move_routing(lines)
        if not beat_move_routing.get("ok"):
            issues.extend(beat_move_routing.get("issues", []))
        move_display = _verify_edit_move_display(lines)
        if not move_display.get("ok"):
            issues.extend(move_display.get("issues", []))
        if record_layout is not None:
            m0_warmup = _verify_m0_survives_warmup(lines, layout=record_layout)
            if not m0_warmup.get("ok"):
                issues.extend(m0_warmup.get("issues", []))

    insert_reorder: Optional[dict[str, object]] = None
    if verify_move_display and record_layout is not None:
        insert_reorder = _verify_delay_move_insert_reorder(lines, layout=record_layout)
        if not insert_reorder.get("ok"):
            issues.extend(insert_reorder.get("issues", []))

    long_over_short_pitch: Optional[dict[str, object]] = None
    if verify_long_over_short_pitch and record_layout is not None:
        long_over_short_pitch = _verify_long_over_short_pitch_restore(lines, layout=record_layout)
        if not long_over_short_pitch.get("ok"):
            issues.extend(long_over_short_pitch.get("issues", []))

    change_length_store: Optional[dict[str, object]] = None
    if verify_long_over_short_pitch and record_layout is not None:
        change_length_store = _verify_change_length_store_rebuild(
            lines, layout=record_layout
        )
        if not change_length_store.get("ok"):
            issues.extend(change_length_store.get("issues", []))

    split_overlap_note_round_trip: Optional[dict[str, object]] = None
    if verify_long_over_short_pitch and record_layout is not None:
        split_overlap_note_round_trip = _verify_split_overlap_note_round_trip(
            lines, layout=record_layout
        )
        if not split_overlap_note_round_trip.get("ok"):
            issues.extend(split_overlap_note_round_trip.get("issues", []))

    note_lengths: Optional[dict[str, object]] = None
    if verify_note_lengths:
        note_lengths = _verify_note_length_integrity(lines)
        if not note_lengths.get("ok"):
            issues.extend(note_lengths.get("issues", []))

    m8_edit_pass: Optional[dict[str, object]] = None
    if verify_m8_edit_pass:
        m8_edit_pass = _verify_m8_edit_pass_commit(lines)
        if not m8_edit_pass.get("ok"):
            issues.extend(m8_edit_pass.get("issues", []))

    return {
        "ok": len(issues) == 0,
        "issues": issues,
        "markers_found": markers_found,
        "revt_count": len(revt_ticks),
        "revt_ticks": revt_ticks,
        "undo_log_count": undo_log_count,
        "redo_log_count": redo_log_count,
        "session_state_enter": session_state_enter,
        "session_undo_routing": session_undo_routing,
        "warmup_empty_nav_create": warmup_empty_nav_create,
        "move_display": move_display,
        "beat_move_routing": beat_move_routing,
        "m0_warmup": m0_warmup,
        "insert_reorder": insert_reorder,
        "long_over_short_pitch": long_over_short_pitch,
        "change_length_store": change_length_store,
        "split_overlap_note_round_trip": split_overlap_note_round_trip,
        "split_victim_round_trip": split_overlap_note_round_trip,
        "note_lengths": note_lengths,
        "m8_edit_pass": m8_edit_pass,
    }


def _run_same_step_chord_cycle_test(
    out_port: mido.ports.BaseOutput,
    *,
    layout: RecordLayout,
    fixture_step: int,
    phase_wait_ms: int,
) -> list[str]:
    """Sweep fader 1 through each nav slot at a multi-note step."""
    markers: list[str] = []
    step_tick = layout.step_to_tick.get(fixture_step, fixture_step * TICKS_PER_16TH_STEP)
    sixteenth = step_tick // TICKS_PER_16TH_STEP
    slot_indices = [
        i
        for i, slot in enumerate(layout.nav_slots)
        if slot.note_idx >= 0 and slot.rel_tick // TICKS_PER_16TH_STEP == sixteenth
    ]
    if len(slot_indices) < 2:
        print(
            f"[warn] same-step chord test: expected >=2 note slots at step {fixture_step}, "
            f"got {len(slot_indices)}"
        )
        return markers

    for idx in slot_indices:
        _fader1_select_nav_slot_index(out_port, layout=layout, slot_index=idx)
        time.sleep(max(phase_wait_ms, 1) / 1000.0)

    if len(slot_indices) >= 2:
        markers.append("2/3 notes at this position")
    if len(slot_indices) >= 3:
        markers.append("3/3 notes at this position")
    return markers


def _run_overlap_round_trip_case(
    out_port: mido.ports.BaseOutput,
    *,
    layout: RecordLayout,
    press_ms: int,
    phase_wait_ms: int,
    markers: list[str],
) -> None:
    """Long M0 over inner notes: one selection, pitch change, move past, return home; short B over long M0."""

    def pause() -> None:
        time.sleep(max(phase_wait_ms, 1) / 1000.0)

    print(
        f"[edit-hitl] overlap round-trip: select M0 once, move over inner notes "
        f"(step {M0_STEP} -> {LONG_OVER_P0_START_STEP}, no reselect until B)"
    )
    _fader1_select_fixture_note(out_port, layout=layout, fixture_step=M0_STEP)
    _coarse_pause()

    _fader2_move_to_sixteenth_step(
        out_port, layout=layout, fixture_step=LONG_OVER_P0_START_STEP
    )
    markers.append("Will delete completely contained note")
    _coarse_pause()

    print(
        f"[edit-hitl] overlap round-trip: pitch M0 {M0_PITCH} -> {A_PITCH} "
        f"(no fader1 reselect; inner P0 should restore)"
    )
    _fader4_pitch_cc(out_port, A_PITCH)
    markers.append("Note value changed successfully")
    _coarse_pause()

    print(
        f"[edit-hitl] overlap round-trip: move past A@step{A_STEP} "
        f"(step {LONG_OVER_P0_START_STEP} -> {PAST_HIGHER_NOTE_STEP}, still no reselect)"
    )
    _fader2_move_to_sixteenth_step(
        out_port, layout=layout, fixture_step=PAST_HIGHER_NOTE_STEP
    )
    markers.append("POSITION EDIT")
    _coarse_pause()

    print(
        f"[edit-hitl] overlap round-trip: return M0 home "
        f"(step {PAST_HIGHER_NOTE_STEP} -> {M0_STEP}, no reselect)"
    )
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=M0_STEP)
    markers.append("POSITION EDIT")
    _display_pause(phase_wait_ms)

    print(
        f"[edit-hitl] short-over-long: select B once, move over long M0 "
        f"(step {B_STEP} -> {A_STEP}) then back to {B_STEP}"
    )
    _fader1_select_fixture_note(out_port, layout=layout, fixture_step=B_STEP)
    _coarse_pause()
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=A_STEP)
    _coarse_pause()
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=B_STEP)
    markers.append("POSITION EDIT")
    _display_pause(phase_wait_ms)


def _run_long_over_short_pitch_case(
    out_port: mido.ports.BaseOutput,
    *,
    layout: RecordLayout,
    press_ms: int,
    phase_wait_ms: int,
    markers: list[str],
) -> None:
    """Alias for overlap round-trip suite (legacy name)."""
    _run_overlap_round_trip_case(
        out_port,
        layout=layout,
        press_ms=press_ms,
        phase_wait_ms=phase_wait_ms,
        markers=markers,
    )


def _run_standard_move_delete_suite(
    out_port: mido.ports.BaseOutput,
    *,
    layout: RecordLayout,
    press_ms: int,
    phase_wait_ms: int,
    markers: list[str],
    long_over_short_pitch_case: bool = True,
) -> None:
    """Move/overlap/delete suite for EDIT_RECORD_FIXTURE (M0,B,A,P0,D,L,R)."""

    def pause() -> None:
        time.sleep(max(phase_wait_ms, 1) / 1000.0)

    def display_pause() -> None:
        time.sleep(max(DISPLAY_SETTLE_MS, phase_wait_ms) / 1000.0)

    # --- Visible beat moves + same-pitch overlap (M0 pitch 60, P0 @ step 12) ---
    print("[edit-hitl] move M0 +1 beat (fixture step 0 -> 4)")
    _fader1_select_then_wait_for_fader2(out_port, layout=layout, fixture_step=0)
    pause()
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=4)
    markers.append("POSITION EDIT")
    display_pause()

    print("[edit-hitl] move M0 -1 beat (fixture step 4 -> 0)")
    _fader1_select_then_wait_for_fader2(out_port, layout=layout, fixture_step=4)
    pause()
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=0)
    display_pause()

    if long_over_short_pitch_case:
        _extend_m0_for_long_over_short(
            out_port,
            layout=layout,
            press_ms=press_ms,
            phase_wait_ms=phase_wait_ms,
        )
        _run_long_over_short_pitch_case(
            out_port,
            layout=layout,
            press_ms=press_ms,
            phase_wait_ms=phase_wait_ms,
            markers=markers,
        )

    # --- Additional overlap moves (sandwich L/R, delete D, etc.) ---
    print("[edit-hitl] move selected note step 12 -> 8")
    _fader1_select_then_wait_for_fader2(out_port, layout=layout, fixture_step=12)
    pause()
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=8)
    display_pause()

    print("[edit-hitl] move selected note step 8 -> 22 -> 24 (sandwich, no fader1 reselect)")
    pause()
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=22)
    pause()
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=24)
    display_pause()

    # --- D delay move: E4@18 onto G4@8, then pitch 64→67 (merge / remove) ---
    print(
        f"[edit-hitl] D delay move: step {D_STEP} -> {A_STEP}, "
        f"pitch {D_PITCH} -> {A_PITCH}"
    )
    _fader1_select_then_wait_for_fader2(out_port, layout=layout, fixture_step=D_STEP)
    pause()
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=A_STEP)
    markers.append("POSITION EDIT")
    display_pause()
    _fader4_pitch_cc(out_port, A_PITCH)
    markers.append("Note value fader")
    display_pause()

    # --- Delete B, insert at empty step 18, move M0 over the new note (nav / reorder) ---
    print(f"[edit-hitl] delete B @ fixture step {B_STEP}")
    _fader1_select_sixteenth_step(out_port, layout=layout, fixture_step=B_STEP)
    pause()
    _delete_selected_note(out_port, press_ms=press_ms)
    markers.append("Deleting note")
    time.sleep(POST_DELETE_QUIET_MS / 1000.0)

    print(f"[edit-hitl] insert note @ empty fixture step {INSERT_NOTE_STEP}")
    display_pause()
    _fader1_select_empty_fixture_step(
        out_port, layout=layout, fixture_step=INSERT_NOTE_STEP
    )
    display_pause()
    _create_note_at_bracket(out_port, press_ms=press_ms)
    markers.append("Created 32nd note")
    display_pause()

    print(
        f"[edit-hitl] move P0 (pitch {M0_PITCH}) over inserted note "
        f"(step {M0_POST_SANDWICH_STEP} -> {INSERT_NOTE_STEP})"
    )
    _fader1_select_sixteenth_step(
        out_port, layout=layout, fixture_step=M0_POST_SANDWICH_STEP
    )
    pause()
    _fader2_move_to_sixteenth_step(
        out_port, layout=layout, fixture_step=INSERT_NOTE_STEP
    )
    markers.append("POSITION EDIT")
    display_pause()

    print(
        f"[edit-hitl] move back off inserted note "
        f"(step {INSERT_NOTE_STEP} -> {M0_POST_SANDWICH_STEP}, no fader1 reselect)"
    )
    _fader2_move_to_sixteenth_step(
        out_port, layout=layout, fixture_step=M0_POST_SANDWICH_STEP
    )
    markers.append("POSITION EDIT")
    display_pause()

    print("[edit-hitl] move over empty slots near former D (steps 17 / 19)")
    _fader1_select_then_wait_for_fader2(
        out_port, layout=layout, fixture_step=INSERT_NOTE_STEP
    )
    pause()
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=17)
    pause()
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=19)
    display_pause()

    print("[edit-hitl] pitch change on M0 via fader 4")
    _fader1_select_sixteenth_step(
        out_port, layout=layout, fixture_step=INSERT_NOTE_STEP
    )
    pause()
    _fader4_pitch_cc(out_port, 72)
    display_pause()


def _run_chord_fixture_move_delete_suite(
    out_port: mido.ports.BaseOutput,
    *,
    layout: RecordLayout,
    press_ms: int,
    phase_wait_ms: int,
    markers: list[str],
) -> None:
    """Shorter move/delete for EDIT_SAME_STEP_CHORD_FIXTURE (M0, chord@8, tail@16)."""

    def pause() -> None:
        time.sleep(max(phase_wait_ms, 1) / 1000.0)

    _fader1_select_then_wait_for_fader2(out_port, layout=layout, fixture_step=0)
    pause()
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=4)
    markers.append("POSITION EDIT")
    pause()

    _fader1_select_then_wait_for_fader2(out_port, layout=layout, fixture_step=8, note_ordinal=0)
    pause()
    _fader2_move_to_sixteenth_step(out_port, layout=layout, fixture_step=10)
    pause()

    _fader1_select_sixteenth_step(out_port, layout=layout, fixture_step=16)
    pause()
    _delete_selected_note(out_port, press_ms=press_ms)
    markers.append("Deleting note")
    pause()


def _run_edit_scenarios(
    out_port: mido.ports.BaseOutput,
    *,
    layout: RecordLayout,
    record_fixture: tuple[FixtureNote, ...],
    press_ms: int,
    phase_wait_ms: int,
    undo_redo_delay_ms: int,
    same_step_chord_step: Optional[int] = None,
    in_edit_undo_redo: bool = True,
    post_exit_undo_redo: bool = True,
    long_over_short_pitch_case: bool = True,
    serial_collector: Optional[SerialCaptureCollector] = None,
    persistence_wait_timeout_s: float = DEFAULT_PERSISTENCE_WAIT_TIMEOUT_S,
    abort: Optional[RunAbort] = None,
) -> list[str]:
    """Run combined edit session; return expected serial marker substrings."""
    markers: list[str] = []

    def pause() -> None:
        time.sleep(max(phase_wait_ms, 1) / 1000.0)

    # Enter edit (wait for deferred short-press so we do not cycle LOOP_EDIT on a 2nd tap)
    _send_short_press(
        out_port,
        note=EDIT_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    markers.append("entered note edit mode")
    time.sleep(max(EDIT_BUTTON_DEBOUNCE_MS, phase_wait_ms) / 1000.0)

    if same_step_chord_step is not None:
        markers.extend(
            _run_same_step_chord_cycle_test(
                out_port,
                layout=layout,
                fixture_step=same_step_chord_step,
                phase_wait_ms=phase_wait_ms,
            )
        )

    # Warmup NOTELEN create/delete on a verified-empty nav slot (never step 0 / M0).
    warmup_step = _warmup_empty_fixture_step(layout)
    if warmup_step is not None:
        if _fader1_select_empty_fixture_step(
            out_port, layout=layout, fixture_step=warmup_step
        ):
            pause()
            _create_note_at_bracket(out_port, press_ms=press_ms)
            markers.append("Created 32nd note")
            pause()
            _delete_selected_note(out_port, press_ms=press_ms)
            markers.append("Deleting note")
            pause()
    else:
        print("[edit-hitl] warmup create/delete skipped (no empty nav slot)")

    if same_step_chord_step is None:
        _run_standard_move_delete_suite(
            out_port,
            layout=layout,
            press_ms=press_ms,
            phase_wait_ms=phase_wait_ms,
            markers=markers,
            long_over_short_pitch_case=long_over_short_pitch_case,
        )
    else:
        _run_chord_fixture_move_delete_suite(
            out_port,
            layout=layout,
            press_ms=press_ms,
            phase_wait_ms=phase_wait_ms,
            markers=markers,
        )

    if in_edit_undo_redo:
        _undo_redo_pair(
            out_port,
            press_ms=press_ms,
            undo_redo_delay_ms=undo_redo_delay_ms,
            phase="in-edit",
        )
        markers.append("EditSession undo")
        markers.append("EditSession redo")
        # Force a deterministic post-redo read path so serial verification can
        # confirm the inserted note is present after in-edit session redo.
        print("[edit-hitl] post-redo probe: enter select mode")
        _send_short_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=press_ms,
        )
        pause()
        print("[edit-hitl] post-redo probe: select insert step")
        _fader1_select_sixteenth_step(
            out_port, layout=layout, fixture_step=INSERT_NOTE_STEP
        )
        pause()

    # Exit edit
    _send_long_press(
        out_port,
        note=EDIT_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=700,
    )
    markers.append("exited edit mode")
    pause()

    if post_exit_undo_redo:
        if serial_collector is not None:
            if _wait_for_persistence_result_after_marker(
                serial_collector,
                marker="exited edit mode",
                timeout_s=persistence_wait_timeout_s,
                abort=abort,
            ):
                print("[edit-hitl] deferred persist complete before post-exit undo")
            else:
                print(
                    "[warn] Timed out waiting for PERS,result,ok after edit exit; "
                    "continuing with post-exit undo"
                )
        _undo_redo_pair(
            out_port,
            press_ms=press_ms,
            undo_redo_delay_ms=undo_redo_delay_ms,
            phase="post-exit",
        )
        markers.append(SCOPED_EDIT_PASS_UNDONE)
        markers.append(SCOPED_EDIT_PASS_REDONE)

    return markers


def main() -> int:
    parser = argparse.ArgumentParser(description="Note-edit HITL automation baseline")
    parser.add_argument("--midi-out", default="Teensy", help="MIDI output port substring")
    parser.add_argument("--midi-in", default="Teensy", help="MIDI input port substring")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--verify-serial-log", type=Path, default=None)
    parser.add_argument("--out-dir", type=Path, default=Path("captures"))
    parser.add_argument("--track", "--track-number", dest="track_number", type=int, default=5)
    parser.add_argument("--midi-channel", type=int, default=None)
    parser.add_argument("--record-bars", type=int, default=2, choices=[1, 2, 4, 8])
    parser.add_argument("--start-transport", action="store_true")
    parser.add_argument("--clear-before-record", action="store_true", default=True)
    parser.add_argument("--no-clear-before-record", action="store_false", dest="clear_before_record")
    parser.add_argument("--clear-press-ms", type=int, default=900)
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--final-wait-ms", type=int, default=3000)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument(
        "--stop-press-advance-clocks",
        type=int,
        default=0,
        help="Send record stop during stream N clocks before end (0 = stop after stream only)",
    )
    parser.add_argument("--state-sync-timeout-ms", type=int, default=8000)
    parser.add_argument(
        "--same-step-chord-record",
        action="store_true",
        help="Record EDIT_SAME_STEP_CHORD_FIXTURE (3 notes on step 8) and run fader-1 cycle test",
    )
    parser.add_argument(
        "--same-step-chord-step",
        type=int,
        default=8,
        help="Fixture step index for same-step chord cycle test (default 8)",
    )
    parser.add_argument(
        "--boot-settle-ms",
        type=int,
        default=0,
        help="Optional wait after opening serial before MIDI actions (default 0)",
    )
    parser.add_argument(
        "--undo-redo-delay-ms",
        type=int,
        default=3000,
        help="Wait before/after in-edit and post-exit undo/redo presses (default 3000)",
    )
    parser.add_argument(
        "--persistence-wait-timeout-s",
        type=float,
        default=DEFAULT_PERSISTENCE_WAIT_TIMEOUT_S,
        help="Max wait for PERS,result,ok after edit exit before post-exit undo (default 30)",
    )
    parser.add_argument(
        "--in-edit-undo-redo",
        action="store_true",
        default=True,
        help="Undo/redo (double/triple record) before exiting edit mode (default on)",
    )
    parser.add_argument(
        "--no-in-edit-undo-redo",
        action="store_false",
        dest="in_edit_undo_redo",
        help="Skip in-edit undo/redo",
    )
    parser.add_argument(
        "--post-exit-undo-redo",
        action="store_true",
        default=True,
        help="Undo/redo after long-press exit from edit mode (default on)",
    )
    parser.add_argument(
        "--no-post-exit-undo-redo",
        action="store_false",
        dest="post_exit_undo_redo",
        help="Skip post-exit undo/redo",
    )
    parser.add_argument(
        "--long-over-short-pitch-case",
        action="store_true",
        default=True,
        help="Record long M0 and run long-over-short + pitch restore scenario (default on)",
    )
    parser.add_argument(
        "--no-long-over-short-pitch-case",
        action="store_false",
        dest="long_over_short_pitch_case",
        help="Skip long-over-short pitch restore scenario",
    )
    parser.add_argument(
        "--require-m8-pass-verify",
        action="store_true",
        default=True,
        help="Require exactly one NoteEditSessionCommitted between enter/exit edit (default on)",
    )
    parser.add_argument(
        "--no-m8-pass-verify",
        action="store_false",
        dest="require_m8_pass_verify",
        help="Skip M8 edit-pass commit count check (pre-M8 capture replay)",
    )
    parser.add_argument(
        "--require-m8-span-verify",
        action="store_true",
        dest="require_m8_pass_verify",
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--require-live-record-display",
        action="store_true",
        default=False,
        help="Fail when RECORD piano-roll frameNotes stay zero while capture grows (D1 regression)",
    )
    args = parser.parse_args()

    record_fixture = (
        EDIT_SAME_STEP_CHORD_FIXTURE
        if args.same_step_chord_record
        else EDIT_RECORD_FIXTURE
    )
    same_step_chord_step = (
        args.same_step_chord_step if args.same_step_chord_record else None
    )

    track_index = args.track_number - 1
    midi_channel = args.midi_channel if args.midi_channel is not None else args.track_number

    out_name = _find_midi_port(args.midi_out, is_input=False)
    in_name = _find_midi_port(args.midi_in, is_input=True)
    out_port = mido.open_output(out_name)
    in_port = mido.open_input(in_name)

    serial_collector: Optional[SerialCaptureCollector] = None
    if args.serial_port:
        serial_collector = SerialCaptureCollector(args.serial_port, args.serial_baud)
        serial_collector.start()
        if args.boot_settle_ms > 0:
            print(f"[edit-hitl] boot settle {args.boot_settle_ms}ms")
            time.sleep(args.boot_settle_ms / 1000.0)

    abort = RunAbort(serial_collector=serial_collector)

    try:
        if args.start_transport:
            if not _ensure_transport_running(
                out_port,
                in_port,
                press_ms=args.press_ms,
                phase_wait_ms=args.phase_wait_ms,
            ):
                print("[warn] MIDI clock still missing before track select")

        track_note = 60 + track_index
        _send_short_press(
            out_port,
            note=track_note,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=args.press_ms,
        )
        time.sleep(args.phase_wait_ms / 1000.0)

        transport_stopped_for_clear = False
        if args.start_transport:
            transport_stopped_for_clear = _stop_transport_if_running(
                out_port,
                in_port,
                press_ms=args.press_ms,
                phase_wait_ms=args.phase_wait_ms,
            )
            if transport_stopped_for_clear:
                drained = _drain_input_messages(in_port)
                if drained:
                    print(
                        f"[edit-hitl] drained {drained} stale MIDI input messages "
                        "after transport stop"
                    )

        if args.clear_before_record:
            if serial_collector is not None:
                snap = serial_collector.snapshot()
                if _can_skip_clear_for_record(snap):
                    latest = _latest_track_state(snap)
                    print(
                        f"[edit-hitl] skipping clear (latest={latest}); proceed to arm/record"
                    )
                elif not _ensure_clear_to_empty(
                    out_port,
                    serial_collector,
                    clear_press_ms=args.clear_press_ms,
                    state_sync_timeout_ms=args.state_sync_timeout_ms,
                    abort=abort,
                ):
                    print("[error] Clear did not reach EMPTY; aborting before record")
                    return 1
                elif not _track_cleared_for_record(serial_collector.snapshot()):
                    latest = _latest_track_state(serial_collector.snapshot())
                    print(
                        f"[error] Track not cleared for record (latest={latest}). "
                        "Stop transport and clear the selected loop, then retry."
                    )
                    return 1
            else:
                print("[edit-hitl] clear selected loop (long press record, no serial)")
                _send_short_press(
                    out_port,
                    note=RECORD_BUTTON_NOTE,
                    channel_1based=CONTROL_CHANNEL_1BASED,
                    press_ms=args.clear_press_ms,
                )
                time.sleep(args.phase_wait_ms / 1000.0)

        if args.start_transport and transport_stopped_for_clear:
            if _clock_seen_within(in_port, 0.5):
                print(
                    "[warn] MIDI clock still running after transport stop; "
                    "stopping transport before arm/record"
                )
                _stop_transport_if_running(
                    out_port,
                    in_port,
                    press_ms=args.press_ms,
                    phase_wait_ms=args.phase_wait_ms,
                )
                _drain_input_messages(in_port)

        print(f"[edit-hitl] record {args.record_bars} bars (fixture, ch{midi_channel})")

        reached_recording = False
        if serial_collector is not None:
            reached_recording = _ensure_recording_started(
                out_port,
                serial_collector,
                press_ms=args.press_ms,
                state_sync_timeout_ms=args.state_sync_timeout_ms,
                abort=abort,
            )
            if not reached_recording:
                latest = _latest_track_state(serial_collector.snapshot())
                print(
                    "[error] Record did not start (no RECA / ->RECORDING in serial). "
                    f"Latest track state={latest}. "
                    "If the track was PLAYING at test start, transport reset + clear should "
                    "have reached EMPTY first — check serial log."
                )
                return 1
        else:
            _send_short_press(
                out_port,
                note=RECORD_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=args.press_ms,
            )
            time.sleep(min(args.phase_wait_ms, 120) / 1000.0)
            _send_short_press(
                out_port,
                note=RECORD_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=args.press_ms,
            )
            time.sleep(min(args.phase_wait_ms, 120) / 1000.0)
            reached_recording = True

        if not reached_recording:
            return 1

        note_count, clocks = _stream_fixture_record(
            out_port,
            in_port,
            fixture=record_fixture,
            target_bars=args.record_bars,
            midi_channel_1based=midi_channel,
            stop_press_advance_clocks=args.stop_press_advance_clocks,
            press_ms=args.press_ms,
            abort=abort,
        )
        print(f"[edit-hitl] fixture note-ons={note_count} clocks={clocks}")
        if note_count < len(record_fixture):
            print(
                f"[error] Fixture incomplete: sent {note_count}/{len(record_fixture)} "
                f"note-ons with {clocks} clocks"
            )
            return 1

        time.sleep(max(args.phase_wait_ms, 0) / 1000.0)
        expected_play_count: Optional[int] = None
        if serial_collector is not None:
            counts = _count_capture_transitions(serial_collector.snapshot())
            expected_play_count = counts.get(("STOPPED_RECORDING", "PLAYING"), 0) + 1

        if args.stop_press_advance_clocks <= 0:
            _send_short_press(
                out_port,
                note=RECORD_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=args.press_ms,
            )

        if serial_collector is not None:
            if expected_play_count is not None:
                _wait_for_transition_count(
                    serial_collector,
                    from_state="STOPPED_RECORDING",
                    to_state="PLAYING",
                    target_count=expected_play_count,
                    timeout_s=args.state_sync_timeout_ms / 1000.0,
                    abort=abort,
                )
            _wait_for_revt_count(
                serial_collector,
                min_count=len(record_fixture),
                timeout_s=max(args.final_wait_ms / 1000.0, 3.0),
                abort=abort,
            )

        ideal_revt = [(n.step * TICKS_PER_16TH_STEP, n.pitch) for n in record_fixture]
        record_layout = RecordLayout(
            loop_start=0,
            loop_length=args.record_bars * TICKS_PER_BAR,
            step_to_tick=_build_fixture_step_to_tick(
                ideal_revt, record_fixture, loop_length=args.record_bars * TICKS_PER_BAR
            ),
            nav_slots=_build_select_navigation_slots(
                ideal_revt,
                loop_length=args.record_bars * TICKS_PER_BAR,
                loop_start=0,
            ),
        )
        if serial_collector is not None:
            parsed_layout = _record_layout_from_serial(
                serial_collector.snapshot(),
                record_bars=args.record_bars,
                fixture=record_fixture,
            )
            if parsed_layout.loop_length > 0:
                record_layout = parsed_layout
            else:
                print("[warn] Serial record layout empty; using ideal fixture layout")
            print(
                f"[edit-hitl] record layout: loop_start={record_layout.loop_start} "
                f"length={record_layout.loop_length} nav_slots={record_layout.nav_slot_count} "
                f"sixteenth_steps={record_layout.sixteenth_steps} "
                f"m0_tick={record_layout.step_to_tick.get(0)}"
            )

        print("[edit-hitl] combined edit scenarios")
        expected_markers = _run_edit_scenarios(
            out_port,
            layout=record_layout,
            record_fixture=record_fixture,
            press_ms=args.press_ms,
            phase_wait_ms=args.phase_wait_ms,
            undo_redo_delay_ms=args.undo_redo_delay_ms,
            same_step_chord_step=same_step_chord_step,
            in_edit_undo_redo=args.in_edit_undo_redo,
            post_exit_undo_redo=args.post_exit_undo_redo,
            long_over_short_pitch_case=args.long_over_short_pitch_case,
            serial_collector=serial_collector,
            persistence_wait_timeout_s=args.persistence_wait_timeout_s,
            abort=abort,
        )

        min_undo_logs = int(args.in_edit_undo_redo) + int(args.post_exit_undo_redo)
        min_redo_logs = min_undo_logs

        time.sleep(args.final_wait_ms / 1000.0)

        verification_lines: list[str] = []
        if serial_collector is not None:
            verification_lines = serial_collector.snapshot()
        elif args.verify_serial_log and args.verify_serial_log.is_file():
            verification_lines = args.verify_serial_log.read_text(encoding="utf-8", errors="replace").splitlines()

        args.out_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        report_path = args.out_dir / f"host_midi_automation_edit_baseline_{stamp}.json"

        standard_edit_fixture = not args.same_step_chord_record
        serial_verification: Optional[dict[str, object]] = None
        if verification_lines:
            has_cap = any("#CAP" in line for line in verification_lines)
            edit_check = _verify_edit_serial(
                verification_lines,
                expected_markers=expected_markers,
                min_revt_count=len(record_fixture),
                min_undo_logs=min_undo_logs,
                min_redo_logs=min_redo_logs,
                verify_move_display=standard_edit_fixture,
                verify_long_over_short_pitch=(
                    standard_edit_fixture and args.long_over_short_pitch_case
                ),
                verify_m8_edit_pass=args.require_m8_pass_verify,
                record_layout=record_layout if standard_edit_fixture else None,
            )
            live_record_display = _verify_live_record_display(verification_lines)
            serial_verification = {
                "has_cap_lines": has_cap,
                "state_counts": _count_capture_state_entries(verification_lines),
                "transitions": {
                    f"{a}->{b}": c
                    for (a, b), c in _count_capture_transitions(verification_lines).items()
                },
                "record": {"live_record_display": live_record_display},
                "edit": edit_check,
            }

        report: dict[str, object] = {
            "script": "host_midi_automation_edit_baseline.py",
            "track_number": args.track_number,
            "midi_channel": midi_channel,
            "record_bars": args.record_bars,
            "same_step_chord_record": args.same_step_chord_record,
            "in_edit_undo_redo": args.in_edit_undo_redo,
            "post_exit_undo_redo": args.post_exit_undo_redo,
            "undo_redo_delay_ms": args.undo_redo_delay_ms,
            "long_over_short_pitch_case": args.long_over_short_pitch_case,
            "require_m8_pass_verify": args.require_m8_pass_verify,
            "require_live_record_display": args.require_live_record_display,
            "fixture_note_count": len(record_fixture),
            "fixture_notes_sent": note_count,
            "record_layout": {
                "loop_start": record_layout.loop_start,
                "loop_length": record_layout.loop_length,
                "nav_slot_count": record_layout.nav_slot_count,
                "sixteenth_steps": record_layout.sixteenth_steps,
            },
            "expected_serial_markers": expected_markers,
            "serial_verification": serial_verification,
        }

        if verification_lines:
            serial_log_path = args.out_dir / f"host_midi_automation_edit_baseline_{stamp}_serial.log"
            serial_log_path.write_text("\n".join(verification_lines) + "\n", encoding="utf-8")
            report["serial_log_path"] = str(serial_log_path)

        report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"[edit-hitl] report written: {report_path}")

        if serial_verification is not None:
            edit_ok = serial_verification.get("edit", {}).get("ok", False)
            print(f"[edit-hitl] serial edit verification ok={edit_ok}")
            move_display = serial_verification.get("edit", {}).get("move_display")
            if move_display:
                print(
                    f"  move/display: beat_fwd={move_display.get('beat_forward')} "
                    f"beat_back={move_display.get('beat_backward')} "
                    f"same_pitch={move_display.get('same_pitch_overlap')} "
                    f"dnte_count={move_display.get('dnte_count')}"
                )
            insert_reorder = serial_verification.get("edit", {}).get("insert_reorder")
            if insert_reorder:
                print(
                    f"  insert/reorder: d_delay={insert_reorder.get('d_delay_move_to_a')} "
                    f"pitch_merge={insert_reorder.get('pitch_merge_64_to_67')} "
                    f"move_over_insert={insert_reorder.get('move_over_inserted')} "
                    f"mover_p60={insert_reorder.get('insert_mover_same_pitch')} "
                    f"deleted={insert_reorder.get('insert_deleted_on_move_over')} "
                    f"restored={insert_reorder.get('insert_restored_after_move_back')} "
                    f"redo_ok={insert_reorder.get('insert_present_after_in_edit_redo')} "
                    f"created={insert_reorder.get('created_32nd_count')}"
                )
            long_over_short = serial_verification.get("edit", {}).get("long_over_short_pitch")
            if long_over_short:
                print(
                    f"  overlap round-trip: contained={long_over_short.get('contained_delete')} "
                    f"p0_restore={long_over_short.get('p0_restore_event')} "
                    f"home={long_over_short.get('round_trip_home')} "
                    f"no_reselect={long_over_short.get('no_reselect_before_pitch')} "
                    f"short_fwd={long_over_short.get('short_over_long_forward')} "
                    f"short_back={long_over_short.get('short_over_long_restore')} "
                    f"inner_b/a/p0={long_over_short.get('inner_b_ok')}/"
                    f"{long_over_short.get('inner_a_visible_after_move_past')}/"
                    f"{long_over_short.get('inner_p0_ok')}"
                )
            change_length_store = serial_verification.get("edit", {}).get(
                "change_length_store"
            )
            if change_length_store:
                print(
                    f"  change-length store: commits={change_length_store.get('change_length_commit_count')} "
                    f"post_commit_snap={change_length_store.get('post_commit_snapshot_line')} "
                    f"pitch_change={change_length_store.get('overlap_pitch_change_line')} "
                    f"post_pitch_snap={change_length_store.get('post_pitch_snapshot_line')} "
                    f"reselect_b={change_length_store.get('reselect_b_line')} "
                    f"post_reselect_snap={change_length_store.get('post_reselect_b_snapshot_line')}"
                )
            split_round_trip = serial_verification.get("edit", {}).get(
                "split_overlap_note_round_trip"
            ) or serial_verification.get("edit", {}).get("split_victim_round_trip")
            if split_round_trip:
                print(
                    f"  split-overlap-note AC: pitch_ok={split_round_trip.get('inner_a_after_pitch')} "
                    f"past_ok={split_round_trip.get('inner_a_visible_after_move_past')} "
                    f"home_ok={split_round_trip.get('inner_a_recaptured_at_home')} "
                    f"no_cross_shorten={split_round_trip.get('cross_pitch_no_shorten_on_long_m0')}"
                )
            note_lengths = serial_verification.get("edit", {}).get("note_lengths")
            if note_lengths:
                print(
                    f"  note-lengths: ok={note_lengths.get('ok')} "
                    f"snapshots={note_lengths.get('snapshot_count')} "
                    f"issues={len(note_lengths.get('issues', []))}"
                )
                for event in note_lengths.get("deterioration_events", [])[:5]:
                    print(f"    deteriorated: {event}")
                for row in note_lengths.get("below_peak_at_end", [])[:5]:
                    print(f"    below_peak: {row}")
            session_state_enter = serial_verification.get("edit", {}).get("session_state_enter")
            if session_state_enter:
                print(
                    f"  session-state enter: ok={session_state_enter.get('ok')} "
                    f"select={session_state_enter.get('select_seen')} "
                    f"cycle_on_enter={session_state_enter.get('cycle_on_enter')}"
                )
            session_undo_routing = serial_verification.get("edit", {}).get("session_undo_routing")
            if session_undo_routing:
                print(
                    f"  session undo routing: in_edit_undo={session_undo_routing.get('in_edit_undo')} "
                    f"in_edit_redo={session_undo_routing.get('in_edit_redo')} "
                    f"post_exit_undo={session_undo_routing.get('post_exit_undo')} "
                    f"post_exit_redo={session_undo_routing.get('post_exit_redo')}"
                )
            warmup_create = serial_verification.get("edit", {}).get("warmup_empty_nav_create")
            if warmup_create:
                print(
                    f"  warmup empty-nav create: ok={warmup_create.get('ok')} "
                    f"action={warmup_create.get('notelen_action')} "
                    f"created={warmup_create.get('created_seen')}"
                )
            live_record_display = serial_verification.get("record", {}).get("live_record_display")
            if live_record_display:
                print(
                    f"  live record display: ok={live_record_display.get('ok')} "
                    f"samples={live_record_display.get('disp_samples')} "
                    f"frame+={live_record_display.get('frame_positive_samples')} "
                    f"sustained_zero={live_record_display.get('max_sustained_frame_zero')}"
                )
                if live_record_display.get("issues"):
                    print(f"    live_record_display issues: {live_record_display.get('issues')}")
            if not edit_ok:
                print(f"  issues: {serial_verification['edit'].get('issues')}")
                return 1
            if (
                args.require_live_record_display
                and live_record_display is not None
                and not live_record_display.get("ok", False)
            ):
                return 1
        return 0
    finally:
        if serial_collector is not None:
            serial_collector.stop()
        out_port.close()
        in_port.close()


if __name__ == "__main__":
    import sys
    from pathlib import Path

    if "-h" in sys.argv or "--help" in sys.argv:
        raise SystemExit(main())

    _root = Path(__file__).resolve().parent
    if str(_root) not in sys.path:
        sys.path.insert(0, str(_root))
    from hitl.runner import main as hitl_main

    raise SystemExit(hitl_main(["run", "--preset", "edit_full", *sys.argv[1:]]))
