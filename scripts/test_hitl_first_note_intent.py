#!/usr/bin/env python3
"""Unit tests for the per-phase first-note intent (the verifier's offset subtrahend).

The measured first-note offset of a HITL phase contains two different things: where the fixture
*meant* its first note to sound (a musical placement) and everything the run was late by (host
reaction, USB, device capture). `capture_latency_clocks` is only a latency if the intent is
subtracted, and the intent is owned by the stream shape that actually ran -- these tests pin both
halves, including the emitter's own ceiling-minus-one granularity, which must NOT be declared as
intent.
"""

from __future__ import annotations

import argparse
import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

import mido  # noqa: E402

from hitl.legacy_edit_baseline import (  # noqa: E402
    EDIT_RECORD_FIXTURE,
    FixtureNote,
    _emit_fixture_steps_for_clock,
)
from hitl.legacy_record_baseline import (  # noqa: E402
    FIRST_NOTE_INTENT_ROW_KEYS,
    _build_serial_verification,
    _fixture_first_note_intent_clocks,
    _first_note_intents,
    _stream_fixture_record_bars,
    _stream_fixture_record_serial_anchored,
    _stream_fixture_record_wall_scheduled,
)
from hitl.transport_clock import stream_pattern_for_bars  # noqa: E402

#: One clock at 300 BPM (24 PPQN).
US_PER_CLOCK_300BPM = 60_000_000.0 / (300.0 * 24.0)


class _FakePort:
    """Records the messages a streamer sends; satisfies mido's output shape by duck typing."""

    def __init__(self) -> None:
        self.sent: list[mido.Message] = []

    def send(self, message: mido.Message) -> None:
        self.sent.append(message)

    def note_ons(self) -> list[mido.Message]:
        return [m for m in self.sent if m.type == "note_on" and m.velocity > 0]

    def poll(self):  # pragma: no cover - only the pacing modes under test do not poll
        return None


class _FakeAbort:
    def check(self):
        return None


class _StubCollector:
    """Only `snapshot()` is used by the tick anchor."""

    def __init__(self, lines: list[str] | None = None) -> None:
        self._lines = list(lines or [])

    def snapshot(self) -> list[str]:
        return list(self._lines)


class _StubClock:
    """The subset of `MidiClockReader` the fixture streamer paces on."""

    def __init__(self, index: int = 0, bpm: float = 600.0) -> None:
        self._index = index
        self._bpm = bpm

    def index_now(self) -> int:
        return self._index

    def period(self) -> float | None:
        return 60.0 / self._bpm / 24.0

    def bpm(self) -> float:
        return self._bpm

    def poll(self) -> None:
        return None

    def observed_pulses(self) -> int:
        return 0

    def gap_count(self) -> int:
        return 0

    def wait_pulse(self, _target, *, timeout_s: float = 0.0, abort=None) -> bool:  # noqa: ANN001
        return True


def _fixture_by_step(fixture: tuple[FixtureNote, ...]) -> dict[int, list[FixtureNote]]:
    by_step: dict[int, list[FixtureNote]] = {}
    for note in fixture:
        by_step.setdefault(note.step, []).append(note)
    return by_step


def _first_emitting_clock(fixture: tuple[FixtureNote, ...], *, step_clocks: int) -> int:
    """Drive the real emitter until it sends the fixture's first note; return that clock."""
    port = _FakePort()
    by_step = _fixture_by_step(fixture)
    for clock in range(1, 64):
        emitted, _next_step, _held = _emit_fixture_steps_for_clock(
            phase_clock=clock,
            step_clocks=step_clocks,
            target_bars=2,
            fixture_by_step=by_step,
            next_emit_step=0,
            out_port=port,
            ch=0,
            held=[],
        )
        if emitted:
            return clock
    raise AssertionError("the emitter never sent the fixture's first note")


class EmitterGranularityTests(unittest.TestCase):
    """The 12-vs-13 question: musical placement vs the clock the emitter lands on."""

    def test_step_two_is_emitted_on_clock_thirteen(self) -> None:
        fixture = (FixtureNote(2, 60),)
        self.assertEqual(_first_emitting_clock(fixture, step_clocks=6), 13)

    def test_declared_intent_is_the_musical_twelve_so_one_clock_stays_latency(self) -> None:
        fixture = (FixtureNote(2, 60),)
        intent = _fixture_first_note_intent_clocks(fixture, step_clocks=6)
        self.assertEqual(intent, 12)
        self.assertEqual(_first_emitting_clock(fixture, step_clocks=6) - intent, 1)

    def test_a_downbeat_fixture_declares_zero_even_though_it_emits_on_clock_one(self) -> None:
        fixture = (FixtureNote(0, 60), FixtureNote(4, 64))
        self.assertEqual(_fixture_first_note_intent_clocks(fixture, step_clocks=6), 0)
        self.assertEqual(_first_emitting_clock(fixture, step_clocks=6), 1)


class DeclaredIntentTests(unittest.TestCase):
    """Each stream shape declares its own placement, with no inflation from what it measured."""

    def test_wall_scheduled_mode_declares_the_fixture_first_step(self) -> None:
        port = _FakePort()
        _notes, _clocks, _stop, intent = _stream_fixture_record_wall_scheduled(
            port,
            fixture=(FixtureNote(2, 60),),
            target_bars=0,
            midi_channel_1based=8,
            record_start_mono=0.0,
            bpm=600.0,
            abort=_FakeAbort(),
        )
        self.assertEqual(intent, 12)

    def test_serial_anchored_mode_declares_the_fixture_first_step(self) -> None:
        # A non-zero first step is the discriminating input: a declaration hardcoded to the record
        # phase's old constant (0) has to fail here.
        port = _FakePort()
        notes, _clocks, intent = _stream_fixture_record_serial_anchored(
            port,
            _StubCollector(),
            fixture=(FixtureNote(2, 60),),
            target_bars=1,
            midi_channel_1based=8,
            reca_start_tick=0,
            bpm=600.0,
            abort=_FakeAbort(),
        )
        self.assertGreaterEqual(notes, 1)
        self.assertEqual(_first_emitting_clock((FixtureNote(2, 60),), step_clocks=6), 13)
        self.assertEqual(intent, 12)

    def test_serial_anchored_mode_declares_the_edit_fixtures_downbeat(self) -> None:
        # The real configuration the edit seed drives: EDIT_RECORD_FIXTURE starts at step 0, so this
        # shape declares 0 and the measured offset stays latency.
        port = _FakePort()
        notes, _clocks, intent = _stream_fixture_record_serial_anchored(
            port,
            _StubCollector(),
            fixture=EDIT_RECORD_FIXTURE,
            target_bars=1,
            midi_channel_1based=8,
            reca_start_tick=0,
            bpm=600.0,
            abort=_FakeAbort(),
        )
        self.assertGreaterEqual(notes, 1)
        self.assertEqual(intent, 0)

    def test_bars_mode_delegates_the_declaration(self) -> None:
        args = argparse.Namespace(press_ms=120, stop_press_advance_clocks=0, tempo_bpm=600.0)
        notes, _clocks, stream_ok, _stop_sent, intent = _stream_fixture_record_bars(
            _FakePort(),
            _FakePort(),
            None,
            args,
            fixture=(FixtureNote(2, 60),),
            target_bars=1,
            midi_channel_1based=8,
            abort=_FakeAbort(),
            clock=_StubClock(),
        )
        self.assertTrue(stream_ok)
        self.assertGreaterEqual(notes, 1)
        self.assertEqual(intent, 12)

    def test_grid_pattern_declares_the_downbeat_when_it_emits_the_first_step_immediately(self) -> None:
        # The immediate step is the phase's downbeat, so the 16th-note grid step is not "intent":
        # the clock the emitter lands on (1) stays latency, exactly as for the step-indexed shapes.
        timing = self._grid_timing(emit_immediate_first_step=True)
        self.assertEqual(timing["first_note_intent_clocks"], 0)

    def test_grid_pattern_declares_its_grid_step_when_it_does_not_emit_immediately(self) -> None:
        # Without the immediate step the grid's own first step is what the phase intends, and this
        # emitter lands on it exactly (its `>=` on an integer counter has no ceiling artifact).
        timing = self._grid_timing(emit_immediate_first_step=False, step_clocks=6)
        self.assertEqual(timing["first_note_intent_clocks"], 6)

    def test_delayed_overdub_declares_its_own_placement(self) -> None:
        timing = self._grid_timing(
            emit_immediate_first_step=True, phase_start_delay_bars=0, phase_start_delay_beats=1
        )
        self.assertEqual(timing["first_note_intent_clocks"], 24)

    def _grid_timing(self, **overrides: object) -> dict[str, object]:
        kwargs = dict(
            midi_channel_1based=8,
            low_note=48,
            high_note=79,
            step_clocks=6,
            target_bars=1,
            cc_number=1,
            cc_step=1,
            pitch_cycle_bars=2,
            phase_start_delay_bars=0,
            phase_start_delay_beats=0,
            max_seconds_guard=10.0,
            clock=_StubClock(),
            emit_immediate_first_step=True,
            abort=_FakeAbort(),
        )
        kwargs.update(overrides)
        _notes, _cc, _clocks, timing = stream_pattern_for_bars(_FakePort(), _FakePort(), **kwargs)
        return timing


class IntentMappingTests(unittest.TestCase):
    """One mapping, owned by the streamer, with a named fallback when it declared nothing."""

    def test_streamer_declaration_wins_over_the_run_default(self) -> None:
        args = _verifier_args(overdub_start_delay_bars=0, overdub_start_delay_beats=0)
        intents = _first_note_intents(
            args=args,
            per_track_stats=[
                {
                    FIRST_NOTE_INTENT_ROW_KEYS["record"]: 12,
                    FIRST_NOTE_INTENT_ROW_KEYS["overdub"]: 24,
                    FIRST_NOTE_INTENT_ROW_KEYS["second_overdub"]: 24,
                }
            ],
        )
        self.assertEqual(
            {phase: (intent.clocks, intent.source) for phase, intent in intents.items()},
            {"record": (12, "streamer"), "overdub": (24, "streamer"), "second_overdub": (24, "streamer")},
        )

    def test_declaring_nothing_falls_back_to_the_run_declared_delay(self) -> None:
        args = _verifier_args(
            overdub_start_delay_bars=0,
            overdub_start_delay_beats=1,
            second_overdub_start_delay_bars=0,
            second_overdub_start_delay_beats=1,
        )
        intents = _first_note_intents(
            args=args, per_track_stats=[{FIRST_NOTE_INTENT_ROW_KEYS["record"]: -1}]
        )
        self.assertEqual(
            {phase: (intent.clocks, intent.source) for phase, intent in intents.items()},
            {
                "record": (0, "run_default"),
                "overdub": (24, "run_default"),
                "second_overdub": (24, "run_default"),
            },
        )

    def test_tracks_that_disagree_fall_back_to_the_run_declared_delay(self) -> None:
        # The row that owns the verified phase window cannot be told from the rows alone (the
        # layered verifier passes no phase window at all), so a disagreement must not subtract
        # another track's placement.
        args = _verifier_args(overdub_start_delay_bars=0, overdub_start_delay_beats=1)
        intents = _first_note_intents(
            args=args,
            per_track_stats=[
                {FIRST_NOTE_INTENT_ROW_KEYS["record"]: 0},
                {FIRST_NOTE_INTENT_ROW_KEYS["record"]: 12},
            ],
        )
        self.assertEqual((intents["record"].clocks, intents["record"].source), (0, "run_default"))

    def test_a_row_that_declares_nothing_also_refuses_the_other_rows_declaration(self) -> None:
        args = _verifier_args(overdub_start_delay_bars=0, overdub_start_delay_beats=1)
        intents = _first_note_intents(
            args=args,
            per_track_stats=[
                {FIRST_NOTE_INTENT_ROW_KEYS["record"]: 12},
                {FIRST_NOTE_INTENT_ROW_KEYS["record"]: -1},
            ],
        )
        self.assertEqual((intents["record"].clocks, intents["record"].source), (0, "run_default"))


class VerifierIntentTests(unittest.TestCase):
    """The verifier subtracts the intent per phase, and real lateness still warns."""

    def test_record_phase_subtracts_the_declared_intent(self) -> None:
        report = self._record_report(first_note_clocks=14, declared_intent=12)
        self.assertEqual(report["record_first_note_offset"]["capture_latency_clocks"], 2)
        self.assertEqual(report["record_first_note_offset"]["intent_source"], "streamer")
        self.assertNotIn("record_first_note_too_late", report["timing_warnings"])

    def test_record_phase_without_a_declaration_uses_the_run_default(self) -> None:
        report = self._record_report(first_note_clocks=14, declared_intent=None)
        self.assertEqual(report["record_first_note_offset"]["capture_latency_clocks"], 14)
        self.assertEqual(report["record_first_note_offset"]["intent_source"], "run_default")
        self.assertIn("record_first_note_too_late", report["timing_warnings"])

    def test_genuine_lateness_still_warns_with_a_declared_intent(self) -> None:
        # intent + bound + 1: one clock past what the 12-clock bound tolerates.
        report = self._record_report(first_note_clocks=12 + 12 + 1, declared_intent=12)
        self.assertEqual(report["record_first_note_offset"]["capture_latency_clocks"], 13)
        self.assertIn("record_first_note_too_late", report["timing_warnings"])

    def test_delayed_overdub_subtracts_its_own_declared_placement(self) -> None:
        report = self._overdub_report(first_note_clocks=28, declared_intent=24)
        self.assertEqual(report["overdub_first_note_offset"]["capture_latency_clocks"], 4)
        self.assertEqual(report["overdub_first_note_offset"]["intent_source"], "streamer")
        self.assertNotIn("overdub_first_note_too_late", report["timing_warnings"])

    def test_overdub_without_a_declaration_falls_back_to_the_run_declared_delay(self) -> None:
        # The run declared no delay for its overdub phases, so the same note is all lateness.
        report = self._overdub_report(first_note_clocks=28, declared_intent=None)
        self.assertEqual(report["overdub_first_note_offset"]["capture_latency_clocks"], 28)
        self.assertEqual(report["overdub_first_note_offset"]["intent_source"], "run_default")
        self.assertIn("overdub_first_note_too_late", report["timing_warnings"])

    def _record_report(
        self, *, first_note_clocks: int, declared_intent: int | None
    ) -> dict[str, object]:
        lines = _phase_lines(
            record_note_clocks=first_note_clocks,
        )
        return _verify(lines, declared_intent=declared_intent, overdub_declared=-1)

    def _overdub_report(
        self, *, first_note_clocks: int, declared_intent: int | None
    ) -> dict[str, object]:
        lines = _phase_lines(overdub_note_clocks=first_note_clocks)
        return _verify(lines, declared_intent=-1, overdub_declared=declared_intent)


def _phase_lines(
    *,
    record_note_clocks: int | None = None,
    overdub_note_clocks: int | None = None,
) -> list[str]:
    """Two phase windows in device microseconds; one first note per phase when asked for."""
    lines = [
        "#CAP,1000,ST,Track,ARMED,RECORDING",
    ]
    if record_note_clocks is not None:
        lines.append(
            f"#CAP,{1000 + round(record_note_clocks * US_PER_CLOCK_300BPM)},MI,U,144,8,48,98"
        )
    lines += [
        "#CAP,1000000,ST,Track,RECORDING,STOPPED_RECORDING",
        "#CAP,2000000,ST,Track,PLAYING,OVERDUBBING",
    ]
    if overdub_note_clocks is not None:
        lines.append(
            f"#CAP,{2000000 + round(overdub_note_clocks * US_PER_CLOCK_300BPM)},MI,U,144,8,24,98"
        )
    lines += [
        "#CAP,3000000,ST,Track,OVERDUBBING,PLAYING",
    ]
    return lines


def _verifier_args(**overrides: object) -> argparse.Namespace:
    base = dict(
        record_only=False,
        midi_channel=8,
        midi_out="Teensy",
        midi_in="Teensy",
        track_number=8,
        loop_slot=4,
        record_bars=2,
        overdub_bars=2,
        second_overdub_bars=0,
        bar_sync_from_midi_clock=True,
        tempo_bpm=300.0,
        transport_timing_source="device_clock",
        record_low_note=48,
        record_high_note=79,
        overdub_low_note=24,
        overdub_high_note=39,
        second_overdub_low_note=12,
        second_overdub_high_note=35,
        overdub_step_clocks=12,
        second_overdub_step_clocks=24,
        overdub_gate_clocks=12,
        second_overdub_gate_clocks=24,
        cc_number=1,
        cc_step=1,
        pitch_cycle_bars=2,
        overdub_start_delay_bars=0,
        overdub_start_delay_beats=0,
        second_overdub_start_delay_bars=0,
        second_overdub_start_delay_beats=0,
        record_first_note_max_clocks=12,
        overdub_first_note_max_clocks=12,
        long_run_bar_threshold=48,
        record_stop_min_free_ram2_bytes=0,
        record_stop_min_free_ram2_warn_bytes=12288,
        occupy_paint_pattern=False,
        overdub_wrap_note_off_test=False,
        successor_reentry_delay_ms=0,
        successor_reentry_stop_delay_ms=0,
    )
    base.update(overrides)
    return argparse.Namespace(**base)


def _verify(
    lines: list[str],
    *,
    declared_intent: int | None,
    overdub_declared: int | None,
) -> dict[str, object]:
    def _value(declared: int | None) -> int:
        return -1 if declared is None else declared

    return _build_serial_verification(
        lines,
        _verifier_args(),
        per_track_stats=[
            {
                "record_notes_sent": 33,
                "record_clock_pulses_seen": 192,
                "overdub_notes_sent": 17,
                "overdub_clock_pulses_seen": 192,
                "second_overdub_notes_sent": 0,
                "second_overdub_clock_pulses_seen": 0,
                FIRST_NOTE_INTENT_ROW_KEYS["record"]: _value(declared_intent),
                FIRST_NOTE_INTENT_ROW_KEYS["overdub"]: _value(overdub_declared),
                FIRST_NOTE_INTENT_ROW_KEYS["second_overdub"]: -1,
            }
        ],
        phase_after_index=0,
    )


class VerifierShapeTests(unittest.TestCase):
    """The declaration the edit fixture's own stream makes, through a real record shape."""

    def test_the_edit_fixture_reaches_the_verifier_as_a_downbeat_declaration(self) -> None:
        # The registry fixture is what the edit seed streams, and the run writes this value into the
        # per-track row the verifier reads: a fixture whose first note is step 0 must declare 0, so
        # the measured offset stays latency for the legs that use it.
        port = _FakePort()
        _notes, _clocks, intent = _stream_fixture_record_serial_anchored(
            port,
            _StubCollector(),
            fixture=EDIT_RECORD_FIXTURE,
            target_bars=1,
            midi_channel_1based=8,
            reca_start_tick=0,
            bpm=600.0,
            abort=_FakeAbort(),
        )
        row = {FIRST_NOTE_INTENT_ROW_KEYS["record"]: intent}
        intents = _first_note_intents(args=_verifier_args(), per_track_stats=[row])
        self.assertEqual((intents["record"].clocks, intents["record"].source), (0, "streamer"))


if __name__ == "__main__":
    unittest.main()
