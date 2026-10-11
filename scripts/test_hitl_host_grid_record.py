#!/usr/bin/env python3
"""Host tests for the host-grid record phase (`--clock-source host`).

The defect these pin: on the base HITL preset a 2-bar record request committed as **3 bars**, and
its first note arrived tens of clocks into the take instead of on the downbeat. Both are host-side
pacing, not device behaviour, and both follow from *not* owning tick 0:

1. the note stream was anchored on when the host happened to look (the serial `RECORDING` row), so
   every note carried that observation delay;
2. the record stop was pressed *after* the stream ended, from wall time, and the device acts on a
   record-button short press only after its double-tap window (`MidiButtonProcessor::onShortRelease`
   -> 300 ms) -- ~0.42 s after the note-on. The committed length is that raw stop tick quantised to
   the nearest bar (`RecordStopLength::quantizeTransportRecordLength`, +/- half a bar), and at
   300 BPM a 0.8 s bar leaves only 0.4 s: the press lands in the next bucket and the take commits
   one bar long. At 120 BPM the same lateness still fits inside half a bar, which is why the
   symptom appeared tempo-dependent.

The fix is to let the python script dictate tick 0: the host drives 24 PPQN (`MidiClockMaster`),
the realtime `Start` it sends after the arm *is* the device's tick 0
(`ClockManager::onMidiStart` resets the tick and holds it through the first pulse;
`TrackManager::handlePendingRecordStart` starts the armed slot at `currentTick == 0`), so the phase
is anchored on that pulse and the stop press is emitted in-stream one press-lead before the
boundary.

Run: .venv/bin/python scripts/test_hitl_host_grid_record.py
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.control_constants import (  # noqa: E402
    MIDI_CLOCKS_PER_BAR,
    RECORD_BUTTON_NOTE,
    TICKS_PER_BAR,
)
from hitl.legacy_record_baseline import (  # noqa: E402
    _host_grid_record_stop_advance_clocks,
    _record_phase_sent_its_own_stop,
    _stream_pressed_the_record_stop,
)
from hitl.midi_clock import MidiClockMaster  # noqa: E402
from hitl.serial_tick_anchor import (  # noqa: E402
    TICKS_PER_CLOCK,
    record_button_lead_seconds,
    record_stop_press_advance_clocks,
    ticks_per_second_from_bpm,
)
from hitl.transport_clock import stream_pattern_for_bars  # noqa: E402


class _NoAbort:
    """A `RunAbort` that never aborts, for the host-path calls that take one."""

    def check(self):
        return None


class _FakeCollector:
    """A serial collector whose rows are whatever the test hands it."""

    def __init__(self, lines=None) -> None:
        self._lines = list(lines or [])

    def snapshot(self) -> list[str]:
        return list(self._lines)


class _RecordingPort:
    """Minimal output port that keeps what was sent, with the master's grid pulse beside it.

    A host-grid phase sends no clock messages of its own -- the pump owns the cadence -- so the only
    way to read *where on the grid* a message was emitted is to sample the master's pulse counter at
    the send. That is the reading the schedule assertions below are stated in.
    """

    def __init__(self, pulse_source=None) -> None:
        self.sent: list[object] = []
        self.pulses: list[int] = []
        self._pulse_source = pulse_source

    def send(self, message: object) -> None:
        self.sent.append(message)
        self.pulses.append(-1 if self._pulse_source is None else int(self._pulse_source()))

    def pulse_of(self, kind: str) -> list[int]:
        """Grid pulse index of every message of ``kind`` (``note_on``/``note_off``/...)."""
        return [
            pulse
            for message, pulse in zip(self.sent, self.pulses)
            if getattr(message, "type", "") == kind
        ]


class _CrankedMaster:
    """A `MidiClockMaster` stand-in whose grid only advances when a caller waits on it.

    The streamer's contract with the master is exactly three reads -- `pump_running`,
    `pulses_sent` and `wait_pulse` -- plus `tick_for_pulse` for the grid evidence, so a hand-cranked
    cursor reproduces the phase arithmetic deterministically and in milliseconds. The *real*
    master's own behaviour (Start declaring the origin, the tick mapping, the pump's absolute
    schedule) is pinned against `MidiClockMaster` itself in
    `test_start_declares_the_grid_origin_and_the_tick_mapping` and in `scripts/test_midi_clock.py`.
    """
    def __init__(
        self,
        *,
        start_pulse: int = 1,
        ticks_per_clock: int = TICKS_PER_CLOCK,
        pump: bool = True,
    ) -> None:
        self._origin = start_pulse
        self._sent = start_pulse - 1
        self._ticks_per_clock = ticks_per_clock
        self._pump = pump
        self.start_calls: list[float | None] = []

    # ---- the streamer's contract ------------------------------------------------
    def pump_running(self) -> bool:
        return self._pump

    def pulses_sent(self) -> int:
        return self._sent

    def wait_pulse(self, index: int, *, timeout_s: float = 1.0, abort: object = None) -> bool:
        del timeout_s, abort
        if index <= self._sent:
            return True
        self._sent = index
        return True

    # ---- grid identity ----------------------------------------------------------
    def grid_origin_pulse(self) -> int:
        return self._origin

    def tick_for_pulse(self, pulse: int) -> int:
        return (pulse - self._origin) * self._ticks_per_clock

    # ---- transport (recorded, never driven by the streamer) ----------------------
    def start(self, *, bpm: float | None = None, abort: object = None) -> None:
        del abort
        self._origin = self._sent + 1
        self.start_calls.append(bpm)

    def stop(self) -> None:
        self.start_calls.append("stop")


class _FakePort:
    """Input port that never delivers anything (the streamer must not poll it in host mode)."""

    def poll(self):
        return None


def _kwargs(**overrides):
    kwargs = dict(
        midi_channel_1based=1,
        low_note=60,
        high_note=64,
        step_clocks=6,
        target_bars=2,
        cc_number=1,
        cc_step=1,
        pitch_cycle_bars=2,
        phase_start_delay_bars=0,
        phase_start_delay_beats=0,
        max_seconds_guard=60.0,
        emit_immediate_first_step=True,
    )
    kwargs.update(overrides)
    return kwargs


class StartGridOriginTests(unittest.TestCase):
    """The realtime `Start` is what makes a pulse device tick 0."""

    def test_start_declares_the_grid_origin_and_the_tick_mapping(self) -> None:
        out = _RecordingPort()
        master = MidiClockMaster(out)  # type: ignore[arg-type]
        # No Start yet: there is no grid to anchor on, and a caller must not invent one.
        self.assertIsNone(master.grid_origin_pulse())
        self.assertIsNone(master.tick_for_pulse(1))

        sent_before = master.pulses_sent()
        master.start(bpm=300.0)
        try:
            origin = master.grid_origin_pulse()
            self.assertIsNotNone(origin)
            # The first pulse the pump sends after Start *is* the origin (grid position 1).
            # Read against the count taken *before* the Start: querying `pulses_sent()` afterwards
            # would race the live pump (8.33 ms period at 300 BPM).
            self.assertEqual(origin, sent_before + 1)
            # ... and it is device tick 0, with 8 ticks per pulse after it
            # (`ClockManager::onMidiClockPulse` holds tick 0 through that first pulse).
            self.assertEqual(master.tick_for_pulse(origin), 0)
            self.assertEqual(master.tick_for_pulse(origin + 1), TICKS_PER_CLOCK)
            self.assertEqual(
                master.tick_for_pulse(origin + MIDI_CLOCKS_PER_BAR), TICKS_PER_BAR
            )
            self.assertEqual(
                master.tick_for_pulse(origin + 2 * MIDI_CLOCKS_PER_BAR), 2 * TICKS_PER_BAR,
                "a two-bar phase's boundary is 192 pulses (and 1536 ticks) in",
            )
        finally:
            master.stop_pump()

    def test_start_without_a_pump_still_declares_the_origin(self) -> None:
        out = _RecordingPort()
        master = MidiClockMaster(out)  # type: ignore[arg-type]
        master.start()
        self.assertEqual(master.grid_origin_pulse(), master.pulses_sent() + 1)

    def test_a_second_start_rebases_the_grid(self) -> None:
        """Each realtime `Start` makes a new tick 0, so the anchor moves with it."""
        out = _RecordingPort()
        master = MidiClockMaster(out)  # type: ignore[arg-type]
        master.start()
        first = master.grid_origin_pulse()
        master.send_pulses(8, bpm=120.0)  # eight pulses of the first grid
        self.assertEqual(master.tick_for_pulse(first + 8), 8 * TICKS_PER_CLOCK)
        master.start()
        second = master.grid_origin_pulse()
        self.assertNotEqual(second, first)
        self.assertEqual(second, master.pulses_sent() + 1)
        self.assertEqual(master.tick_for_pulse(second), 0)
        self.assertNotEqual(master.tick_for_pulse(first), 0, "the old grid is not tick 0")

    def test_the_arm_anchor_is_the_start_not_a_modelled_button_debounce(self) -> None:
        """`_run_record_arm_until_capturing` must not apply the short-press lead in host mode.

        The modelled lead exists for a *button* start; the host's realtime `Start` has no debounce,
        and subtracting one would move the whole host-grid phase off tick 0 by `press_ms` + 300 ms.
        The function under test is the one whose two anchor expressions were changed, so reverting
        either of them (`press_sent if master is not None else …`) fails here.
        """
        import time as _time

        from hitl.legacy_record_baseline import _run_record_arm_until_capturing

        master = _CrankedMaster()
        args = SimpleNamespace(
            press_ms=120,
            phase_wait_ms=1,
            clear_before_record=True,
            state_sync_timeout_ms=200,
            serial_grace_ms=0,
        )
        # A managed run always has serial capture, so the *preset's* path is the one with a
        # collector; the arm gate is satisfied by an ARMED track row, which is the state the arm
        # press is supposed to leave behind.
        collector = _FakeCollector(["#CAP,1000,ST,Track,STOPPED,ARMED"])
        before = _time.monotonic()
        reached, anchor = _run_record_arm_until_capturing(
            _RecordingPort(),
            _FakePort(),
            collector,
            args,
            track_baseline_len=0,
            record_loop_slot=None,
            abort=_NoAbort(),
            wait_for_recording_confirm=False,
            master=master,
            tempo_bpm=300.0,
        )
        after = _time.monotonic()
        self.assertTrue(reached)
        self.assertEqual(
            master.start_calls,
            ["stop", 300.0],
            "a realtime Stop, then the host's realtime Start -- and no transport press",
        )
        self.assertIsNotNone(anchor)
        self.assertLessEqual(before, anchor)
        self.assertLessEqual(anchor, after, "the anchor is the Start instant itself")
        # Two-sided: a *future* anchor (the modelled `press_ms + 300 ms` lead) is just as wrong as a
        # late one, and `after - anchor` would be trivially satisfied by it.
        self.assertLess(
            abs(after - anchor),
            record_button_lead_seconds(press_ms=120) / 2.0,
            "the modelled short-press lead must not be added to a realtime Start",
        )

    def test_the_same_anchor_is_used_without_serial_capture(self) -> None:
        """The no-serial branch has its own copy of the expression; both must be host-aware."""
        import time as _time

        from hitl.legacy_record_baseline import _run_record_arm_until_capturing

        master = _CrankedMaster()
        args = SimpleNamespace(press_ms=120, phase_wait_ms=1, clear_before_record=True)
        before = _time.monotonic()
        reached, anchor = _run_record_arm_until_capturing(
            _RecordingPort(),
            _FakePort(),
            None,
            args,
            track_baseline_len=0,
            record_loop_slot=None,
            abort=_NoAbort(),
            wait_for_recording_confirm=False,
            master=master,
            tempo_bpm=300.0,
        )
        after = _time.monotonic()
        self.assertTrue(reached)
        self.assertEqual(master.start_calls[-1], 300.0)
        self.assertLess(
            abs(after - anchor),
            record_button_lead_seconds(press_ms=120) / 2.0,
            "the no-serial branch carries the same host anchor",
        )


class GridAnchoredPhaseTests(unittest.TestCase):
    """The phase's grid position 1 is the pulse device tick 0 names."""

    def test_step_zero_is_emitted_on_the_tick_zero_pulse(self) -> None:
        master = _CrankedMaster(start_pulse=41)
        out = _RecordingPort(pulse_source=lambda: master.pulses_sent())
        notes, _cc, clocks, timing = stream_pattern_for_bars(
            out,
            _FakePort(),
            master=master,
            wall_clock_tempo_bpm=300.0,
            grid_origin_pulse=master.grid_origin_pulse(),
            **_kwargs(),
        )

        self.assertEqual(timing["clock_source"], "host_clock")
        self.assertEqual(clocks, 2 * MIDI_CLOCKS_PER_BAR)
        self.assertEqual(notes, 32)
        # Every note goes out on the grid, one per pulse: step s is emitted on grid position
        # `1 + 6s`, i.e. the pulse whose device tick is `48 * s`, so the first note is the downbeat.
        self.assertEqual(
            timing["grid_first_note_pulse"], master.grid_origin_pulse(),
            "the first note is emitted on the pulse the Start made tick 0",
        )
        self.assertEqual(timing["grid_first_note_tick"], 0)
        self.assertEqual(timing["grid_origin_pulse"], master.grid_origin_pulse())
        note_pulses = out.pulse_of("note_on")
        self.assertEqual(len(note_pulses), 32)
        self.assertEqual(
            note_pulses,
            [master.grid_origin_pulse() + 6 * step for step in range(32)],
        )
        self.assertEqual(
            [master.tick_for_pulse(pulse) for pulse in note_pulses],
            [48 * step for step in range(32)],
            "step s sits on device tick 48s, i.e. exactly on the 1/16 grid",
        )

    def test_the_device_bar_probe_cannot_truncate_a_grid_anchored_phase(self) -> None:
        """The host owns the grid, so the lagging serial `BAR` probe is not an end condition."""
        probe_calls = {"n": 0}

        def probe() -> int:
            probe_calls["n"] += 1
            return 1000  # far past the baseline: an unanchored phase would stop after one bar

        master = _CrankedMaster(start_pulse=3)
        out = _RecordingPort()
        notes, _cc, clocks, _timing = stream_pattern_for_bars(
            out,
            _FakePort(),
            master=master,
            wall_clock_tempo_bpm=300.0,
            grid_origin_pulse=master.grid_origin_pulse(),
            device_bar_probe=probe,
            device_bar_baseline=0,
            **_kwargs(),
        )
        self.assertEqual(clocks, 2 * MIDI_CLOCKS_PER_BAR, "the phase ran its full grid")
        self.assertEqual(notes, 32, "every step was emitted")
        self.assertEqual(probe_calls["n"], 0, "the probe was not consulted at all")

    def test_an_unanchored_phase_still_uses_the_probe(self) -> None:
        """The control: without the anchor the probe is still the end condition."""
        master = _CrankedMaster(start_pulse=3)
        out = _RecordingPort()
        notes, _cc, clocks, timing = stream_pattern_for_bars(
            out,
            _FakePort(),
            master=master,
            wall_clock_tempo_bpm=300.0,
            device_bar_probe=lambda: 1000,
            device_bar_baseline=0,
            **_kwargs(),
        )
        self.assertEqual(timing["grid_origin_pulse"], -1)
        self.assertLess(clocks, 2 * MIDI_CLOCKS_PER_BAR)
        self.assertLess(notes, 32)

    def test_a_masterless_or_pumpless_anchor_is_refused_by_name(self) -> None:
        # No master at all: there is no pump whose pulses could carry the grid.
        with self.assertRaises(ValueError) as ctx:
            stream_pattern_for_bars(
                _RecordingPort(),
                _FakePort(),
                wall_clock_tempo_bpm=300.0,
                grid_origin_pulse=1,
                **_kwargs(),
            )
        self.assertIn("master", str(ctx.exception))

        # A master that is not pumping: the phase would wait on a pulse nobody sends.
        idle = _CrankedMaster(start_pulse=1, pump=False)
        with self.assertRaises(ValueError) as ctx:
            stream_pattern_for_bars(
                _RecordingPort(),
                _FakePort(),
                master=idle,
                wall_clock_tempo_bpm=300.0,
                grid_origin_pulse=idle.pulses_sent() + 1,
                **_kwargs(),
            )
        self.assertIn("pump", str(ctx.exception))

    def test_an_anchor_the_host_already_passed_degrades_and_reports_drift(self) -> None:
        """A stalled host must not raise out of a phase whose take is already armed.

        The phase falls back to the next pulse -- still one grid position per pulse, so the note
        stream and the stop schedule stay coherent -- and says how far behind the anchor it is. The
        caller refuses such a run on `grid_anchor_missed_pulses`; the *streamer* never throws.
        """
        master = _CrankedMaster(start_pulse=10)
        master.wait_pulse(41)  # three pulses of the Start's grid have already gone
        out = _RecordingPort(pulse_source=lambda: master.pulses_sent())
        notes, _cc, clocks, timing = stream_pattern_for_bars(
            out,
            _FakePort(),
            master=master,
            wall_clock_tempo_bpm=300.0,
            grid_origin_pulse=10,
            **_kwargs(),
        )
        self.assertEqual(timing["grid_origin_pulse"], 10, "the pinned anchor is reported as pinned")
        self.assertEqual(
            timing["grid_anchor_missed_pulses"], 32, "41 - 10 + 1 pulses behind the anchor"
        )
        self.assertEqual(clocks, 2 * MIDI_CLOCKS_PER_BAR, "the phase still ran its full grid")
        self.assertEqual(notes, 32)
        # The first note is on the next pulse, and the evidence says which device tick that is --
        # not 0, which is exactly the reading the caller refuses the run on.
        self.assertEqual(timing["grid_first_note_pulse"], 42)
        self.assertEqual(timing["grid_first_note_tick"], 32 * TICKS_PER_CLOCK)
        self.assertEqual(out.pulse_of("note_on")[0], 42)

class RecordStopPressScheduleTests(unittest.TestCase):
    """The in-stream record stop must land in the requested length's bucket."""

    def test_the_press_lead_is_the_button_model_expressed_in_clocks(self) -> None:
        from math import ceil

        for bpm in (20.0, 120.0, 300.0):
            lead_clocks = record_button_lead_seconds(press_ms=120) * (bpm / 60.0) * 24.0
            self.assertEqual(
                record_stop_press_advance_clocks(bpm=bpm, press_ms=120),
                max(1, ceil(lead_clocks) - 1),
            )
        self.assertEqual(record_stop_press_advance_clocks(bpm=300.0, press_ms=120), 50)
        with self.assertRaises(ValueError):
            record_stop_press_advance_clocks(bpm=0.0, press_ms=120)

    def test_the_stop_effect_lands_in_the_two_bar_bucket_at_every_reachable_tempo(self) -> None:
        """`quantizeTransportRecordLength` commits `record_bars` bars from a raw tick in the bucket.

        The bucket is +/- half a bar around the boundary (firmware constant, cited rather than
        reimplemented): `raw % TICKS_PER_BAR <= TICKS_PER_BAR // 2` floors, otherwise it ceilings,
        so a raw stop tick within half a bar either side of `target_bars * TICKS_PER_BAR` commits
        exactly `target_bars` bars. The scheduled effect -- the press tick plus the button lead the
        device answers it after -- must be inside that window at every tempo the device accepts, and
        the press itself must still be emitted inside the phase.
        """
        target_bars = 2
        target_ticks = target_bars * TICKS_PER_BAR
        half_bar = TICKS_PER_BAR // 2
        for bpm in (20.0, 60.0, 120.0, 200.0, 300.0):
            advance = record_stop_press_advance_clocks(bpm=bpm, press_ms=120)
            self.assertGreaterEqual(advance, 1)
            self.assertLess(advance, target_bars * MIDI_CLOCKS_PER_BAR)

            master = _CrankedMaster(start_pulse=1)
            out = _RecordingPort(pulse_source=lambda: master.pulses_sent())
            _notes, _cc, clocks, timing = stream_pattern_for_bars(
                out,
                _FakePort(),
                master=master,
                wall_clock_tempo_bpm=bpm,
                grid_origin_pulse=master.grid_origin_pulse(),
                stop_press_note=RECORD_BUTTON_NOTE,
                stop_press_advance_clocks=advance,
                stop_press_press_ms=120,
                **_kwargs(),
            )
            self.assertEqual(clocks, target_bars * MIDI_CLOCKS_PER_BAR)
            self.assertEqual(
                timing["stop_press_sent_during_stream"], 1.0,
                "the stop is emitted in-stream, on the grid, not afterwards from wall time",
            )
            # The press pulse: the streamer triggers at grid position `target_clocks - advance`,
            # and grid position c is the pulse whose device tick is `(c - 1) * TICKS_PER_CLOCK`.
            press_pulse = master.grid_origin_pulse() + (target_bars * MIDI_CLOCKS_PER_BAR - advance) - 1
            press_tick = master.tick_for_pulse(press_pulse)
            self.assertGreaterEqual(press_tick, 0, "the press is emitted inside the phase")
            lead_ticks = record_button_lead_seconds(press_ms=120) * ticks_per_second_from_bpm(bpm)
            effect_tick = press_tick + lead_ticks
            self.assertLessEqual(
                abs(effect_tick - target_ticks), half_bar,
                f"at {bpm} BPM the stop effect ({effect_tick:.0f}) must sit in the "
                f"{target_bars}-bar bucket around {target_ticks}",
            )

    def test_the_emitted_press_is_where_the_schedule_says(self) -> None:
        """The press's own note-on must be on the grid pulse the schedule names."""
        bpm = 300.0
        target_clocks = 2 * MIDI_CLOCKS_PER_BAR
        advance = record_stop_press_advance_clocks(bpm=bpm, press_ms=120)
        master = _CrankedMaster(start_pulse=11)
        out = _RecordingPort(pulse_source=lambda: master.pulses_sent())
        stream_pattern_for_bars(
            out,
            _FakePort(),
            master=master,
            wall_clock_tempo_bpm=bpm,
            grid_origin_pulse=master.grid_origin_pulse(),
            stop_press_note=RECORD_BUTTON_NOTE,
            stop_press_advance_clocks=advance,
            stop_press_press_ms=120,
            **_kwargs(),
        )
        presses = [
            (message, pulse)
            for message, pulse in zip(out.sent, out.pulses)
            if getattr(message, "type", "") == "note_on"
            and getattr(message, "note", None) == RECORD_BUTTON_NOTE
        ]
        self.assertEqual(len(presses), 1, "the record stop was pressed in-stream, exactly once")
        _press, press_pulse = presses[0]
        self.assertEqual(
            press_pulse,
            master.grid_origin_pulse() + (target_clocks - advance) - 1,
            "the press is emitted on the grid position the schedule names",
        )
        self.assertEqual(
            master.tick_for_pulse(press_pulse),
            (target_clocks - advance - 1) * TICKS_PER_CLOCK,
        )
        # The note-offs that follow are not extra presses, and the stream still ran its full grid.
        self.assertEqual(
            len(out.pulse_of("note_off")), 33,
            "32 note-offs plus the press release",
        )


class RecordStopWiringTests(unittest.TestCase):
    """The caller must read the emitter's own statement that it pressed."""

    def test_an_in_stream_press_suppresses_the_post_stream_one(self) -> None:
        self.assertTrue(_stream_pressed_the_record_stop({"stop_press_sent_during_stream": 1.0}))
        # Absent / zero / None all mean "the streamer did not press", so the caller must.
        self.assertFalse(_stream_pressed_the_record_stop({}))
        self.assertFalse(_stream_pressed_the_record_stop({"stop_press_sent_during_stream": 0.0}))
        self.assertFalse(
            _stream_pressed_the_record_stop({"stop_press_sent_during_stream": None})
        )

    def test_every_stream_shape_is_read_by_the_composition(self) -> None:
        """The caller's one-stop decision, so a reverted branch is a failing test.

        A second press on a track that is already `PLAYING` again is not a stop, so each shape's own
        statement has to reach this decision: the edit fixture reports it as a return value, and the
        occupy stream and the base grid stream in their `timing` evidence.
        """

        def compose(**overrides):
            kwargs = dict(
                rec_timing={},
                edit_fixture=False,
                fixture_record_stop_sent=False,
                occupy_record_stop_sent=False,
            )
            kwargs.update(overrides)
            return _record_phase_sent_its_own_stop(**kwargs)

        self.assertTrue(compose(rec_timing={"stop_press_sent_during_stream": 1.0}))
        self.assertFalse(compose(), "nothing pressed means the caller must press")
        self.assertTrue(compose(edit_fixture=True, fixture_record_stop_sent=True))
        self.assertFalse(
            compose(fixture_record_stop_sent=True),
            "the edit fixture's return value only counts when that fixture ran",
        )
        self.assertTrue(compose(occupy_record_stop_sent=True))
        self.assertFalse(compose(rec_timing={"stop_press_sent_during_stream": None}))

    def test_the_record_phase_wiring_is_still_in_place(self) -> None:
        """A tripwire for the *call sites*, which no unit can drive without the bench.

        The decisions themselves are pinned above; what this adds is that the record phase still
        routes through them -- the composition call and the arguments the grid-anchored stream needs.
        It is a source scan, in the spirit of this repo's compatibility-inventory gates: it fails if
        the wiring is removed or renamed, and it says nothing about behaviour (the device leg is what
        proves the behaviour).
        """
        import inspect

        from hitl import legacy_record_baseline as module

        source = inspect.getsource(module)
        for required in (
            "record_stop_sent_during_stream = _record_phase_sent_its_own_stop(",
            "_host_grid_record_stop_advance_clocks(",
            "grid_origin_pulse=record_grid_origin_pulse,",
            "stop_press_note=record_stop_press_note,",
            "record_grid_origin_pulse = phase_master.grid_origin_pulse()",
            'abort_reason = "host grid anchor missed before the record phase"',
            'abort_reason = "recording not confirmed after the host Start"',
        ):
            self.assertIn(required, source, f"{required!r} is not wired into the record phase")



class HostGridStopLeadTests(unittest.TestCase):
    """The lead the run uses, and the two ways it refuses to take one it cannot use."""

    def test_the_default_lead_is_the_button_latency_at_this_tempo(self) -> None:
        for bpm in (20.0, 120.0, 300.0):
            advance, refusal = _host_grid_record_stop_advance_clocks(
                requested=0, bpm=bpm, press_ms=120, record_bars=2
            )
            self.assertIsNone(refusal)
            self.assertEqual(advance, record_stop_press_advance_clocks(bpm=bpm, press_ms=120))

    def test_a_caller_lead_inside_the_button_latency_is_raised_not_obeyed(self) -> None:
        """`--stop-press-advance-clocks 1` must not re-create the one-bar-long take.

        The effect tick is `(target_clocks - advance - 1) * TICKS_PER_CLOCK + lead_ticks`; at 300 BPM
        an advance of 1 puts it past the two-bar bucket (`191 * 8 + 403 = 1931 > 1920`), so obeying
        the flag would commit 2304 ticks against a 2-bar request.
        """
        advance, refusal = _host_grid_record_stop_advance_clocks(
            requested=1, bpm=300.0, press_ms=120, record_bars=2
        )
        self.assertIsNone(refusal)
        self.assertEqual(advance, record_stop_press_advance_clocks(bpm=300.0, press_ms=120))
        lead_ticks = record_button_lead_seconds(press_ms=120) * ticks_per_second_from_bpm(300.0)
        effect_tick = (2 * MIDI_CLOCKS_PER_BAR - advance - 1) * TICKS_PER_CLOCK + lead_ticks
        self.assertLessEqual(
            abs(effect_tick - 2 * TICKS_PER_BAR), TICKS_PER_BAR // 2,
            "the raised lead lands the effect in the two-bar bucket",
        )
        # A caller lead larger than the button latency is honoured as given.
        self.assertEqual(
            _host_grid_record_stop_advance_clocks(
                requested=60, bpm=300.0, press_ms=120, record_bars=2
            ),
            (60, None),
        )

    def test_a_lead_that_cannot_fit_inside_the_phase_is_refused_by_name(self) -> None:
        advance, refusal = _host_grid_record_stop_advance_clocks(
            requested=200, bpm=300.0, press_ms=120, record_bars=2
        )
        self.assertEqual(advance, 200)
        self.assertIsNotNone(refusal)
        self.assertIn("does not fit", refusal)
        self.assertIn("2-bar phase", refusal)

        _advance, no_bars = _host_grid_record_stop_advance_clocks(
            requested=0, bpm=300.0, press_ms=120, record_bars=0
        )
        self.assertIn("--record-bars", no_bars)


if __name__ == "__main__":
    unittest.main(verbosity=2)
