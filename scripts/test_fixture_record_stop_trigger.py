"""Regression for the real fixture streamer: its stop trigger, its take grid and its cadence.

Three measured defects are pinned here, all from the edit-fixture record seed
(`note_edit_reselect_stacked` -> `canonical_edit_minimal_base_legacy_args`):

* the streamer never pressed the stop unless a lead was handed to it, so the caller pressed after
  the stream plus the post-stream settle *and* the device's own press latency:
  `captures/i3b-seeded-edit-t8s4-11` processed the stop at tick 2436 for a 1536-tick (two-bar)
  request (`RECS,stop,3,132,0,2436,2304,0`);
* the phase origin was "now" rather than the take origin, so a host that reached the streamer late
  slid the whole fixture: the same capture stored its notes at ticks `378,569,761,...,1721` -- two of
  them outside the intended two-bar loop;
* with the host owning the clock, the phase sent its own pulses while the master's pump was already
  driving, double-driving the device: `captures/i3b-seeded-edit-t8s4-13` recorded 768 ticks per
  0.40 s (1 920 ticks/s) against a 300 BPM request and stored the fixture at 2x spacing
  (`REVT,8/392/776/1160/1736/2120/2504/2696`).
"""

from __future__ import annotations

import sys
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hitl import legacy_edit_baseline as baseline
from hitl.legacy_edit_baseline import EDIT_RECORD_FIXTURE  # noqa: E402
from hitl.legacy_record_baseline import (  # noqa: E402
    MIDI_CLOCKS_PER_BAR,
    _host_grid_record_stop_advance_clocks,
    _stream_fixture_record_bars,
)
from hitl.serial_tick_anchor import TICKS_PER_CLOCK, record_stop_press_advance_clocks  # noqa: E402


class _Clock:
    """A `MidiClockReader` stand-in that records the grid positions the phase waited for.

    `index_now()` is the reader's phase-locked position -- here it is the *late host*: the phase
    starts `behind_clocks` past the take origin, exactly as `captures/i3b-seeded-edit-t8s4-11`
    measured (the take origin at device tick 0, the first fixture note at tick 378 = 47 clocks).
    """

    def __init__(self, *, index_now: float = 47.0, bpm: float = 300.0) -> None:
        self._index_now = index_now
        self._bpm = bpm
        self.waits: list[int] = []
        self.last_wait: int | None = None

    def index_now(self) -> float:
        return self._index_now

    def period(self) -> float:
        return 60.0 / self._bpm / 24.0

    def bpm(self) -> float:
        return self._bpm

    def wait_pulse(self, target, *, timeout_s: float = 0.0, abort=None) -> bool:  # noqa: ANN001
        self.waits.append(int(target))
        self.last_wait = int(target)
        # Past positions are satisfied at once (the catch-up), later ones advance the reader.
        self._index_now = max(self._index_now, float(target))
        return True


class _PumpMaster:
    """A master whose continuous pump owns the grid; it records whether the phase sent its own."""

    def __init__(self, *, origin_pulse: int = 41, cursor: int | None = None, pump: bool = True) -> None:
        self._origin = origin_pulse
        self._sent = cursor if cursor is not None else origin_pulse - 1
        self._pump = pump
        self.sent_pulses: list[int] = []
        self.waits: list[int] = []

    def pump_running(self) -> bool:
        return self._pump

    def pulses_sent(self) -> int:
        return self._sent

    def grid_origin_pulse(self) -> int:
        return self._origin

    def tick_for_pulse(self, pulse: int) -> int:
        return (pulse - self._origin) * TICKS_PER_CLOCK

    def wait_pulse(self, index: int, *, timeout_s: float = 1.0, abort=None) -> bool:  # noqa: ANN001
        self.waits.append(int(index))
        self._sent = max(self._sent, int(index))
        return True

    def send_pulses(self, count: int, **_kwargs) -> dict:
        self.sent_pulses.extend(range(self._sent + 1, self._sent + 1 + count))
        self._sent += count
        return {}


class _Port:
    def __init__(self) -> None:
        self.sent: list[object] = []

    def send(self, message: object) -> None:
        self.sent.append(message)

    def poll(self):  # pragma: no cover - the clock/master modes never poll
        return None

    def note_on_ticks(self) -> list[int]:
        """Device tick of every note-on, from the grid position the phase waited on."""
        return [tick for kind, tick in self._ticks() if kind == "note_on"]

    def _ticks(self) -> list[tuple[str, int]]:
        return [(kind, tick) for kind, tick in self.sent if isinstance(tick, int)]  # type: ignore[misc]


class _Abort:
    def check(self):
        return None


def _run_with_clock(clock, port, **kwargs):
    """Stream the canonical fixture with the send-side grid recorded on each message."""
    recorded = _Port()

    def send(message):
        recorded.sent.append((message.type, _current_grid["grid"], getattr(message, "channel", -1)))
        port.sent.append(message)

    class _Out:
        def send(self, message):
            send(message)

    _current_grid = {"grid": 0}
    original_wait = clock.wait_pulse

    def wait(target, **kw):
        ok = original_wait(target, **kw)
        _current_grid["grid"] = int(target)
        return ok

    clock.wait_pulse = wait  # type: ignore[assignment]
    notes, clocks = baseline._stream_fixture_record(
        _Out(),
        _Port(),
        fixture=EDIT_RECORD_FIXTURE,
        target_bars=2,
        midi_channel_1based=8,
        clock=clock,
        abort=_Abort(),
        max_seconds_guard=10,
        **kwargs,
    )
    fixture_grids = [
        grid for kind, grid, channel in recorded.sent if kind == "note_on" and channel == 7
    ]
    stop_grids = [
        grid for kind, grid, channel in recorded.sent if kind == "note_on" and channel == 15
    ]
    return notes, clocks, fixture_grids, stop_grids


class FixtureRecordStopTest(unittest.TestCase):
    def test_stop_modes(self):
        for advance, expected in ((0, 0), (-1, 1), (12, 1)):
            with self.subTest(advance=advance):
                output, input_port, master = Mock(), Mock(), Mock()
                input_port.poll.return_value = None
                with patch.object(baseline, "_send_short_press") as stop:
                    notes, clocks = baseline._stream_fixture_record(
                        output, input_port, fixture=(), target_bars=1,
                        midi_channel_1based=8, stop_press_advance_clocks=advance,
                        master=master, max_seconds_guard=10,
                    )
                self.assertEqual((notes, clocks), (0, baseline.MIDI_CLOCKS_PER_BAR))
                self.assertEqual(stop.call_count, expected)
                self.assertEqual(master.send_pulses.call_count, baseline.MIDI_CLOCKS_PER_BAR)


class TakeGridPlacementTests(unittest.TestCase):
    """A late host must not slide the fixture: every step keeps its take-relative tick."""

    CANONICAL_STEPS = (0, 4, 8, 12, 18, 22, 26, 28)  # EDIT_RECORD_FIXTURE

    def test_the_device_clock_phase_places_the_fixture_on_the_take_grid(self) -> None:
        clock = _Clock(index_now=47.0)
        notes, clocks, grids, stop_grids = _run_with_clock(
            clock, _Port(), grid_origin_index=0, stop_press_advance_clocks=50
        )
        self.assertEqual(notes, len(EDIT_RECORD_FIXTURE))
        self.assertEqual(clocks, 2 * MIDI_CLOCKS_PER_BAR)
        # Step s is emitted on the grid position `origin + 6s + 1`: the take-relative tick of every
        # note is `48 * step`, so the fixture's own span (0..1344) stays inside the 1536-tick loop
        # the seeding contract asks for. Without the take origin the same run stored 378..1721.
        self.assertEqual(grids, [1 + 6 * step for step in self.CANONICAL_STEPS])
        ticks = [(grid - 1) * TICKS_PER_CLOCK for grid in grids]
        self.assertEqual(ticks, [48 * step for step in self.CANONICAL_STEPS])
        self.assertLess(max(ticks), 2 * 48 * 16, "every fixture note is inside the two-bar loop")
        # The press itself is emitted `advance` clocks before the boundary, so the device's own
        # press latency lands its *effect* on tick 1536 rather than one bar later.
        self.assertEqual(stop_grids, [2 * MIDI_CLOCKS_PER_BAR - 50])

    def test_the_pump_owns_the_cadence_so_the_device_is_not_double_driven(self) -> None:
        master = _PumpMaster(origin_pulse=41, cursor=41 + 47)
        with patch.object(baseline, "_send_short_press"):
            notes, clocks = baseline._stream_fixture_record(
                _Port(),
                _Port(),
                fixture=EDIT_RECORD_FIXTURE,
                target_bars=2,
                midi_channel_1based=8,
                master=master,
                grid_origin_index=master.grid_origin_pulse(),
                stop_press_advance_clocks=50,
                abort=_Abort(),
                max_seconds_guard=10,
            )
        self.assertEqual(notes, len(EDIT_RECORD_FIXTURE))
        self.assertEqual(clocks, 2 * MIDI_CLOCKS_PER_BAR)
        self.assertEqual(
            master.sent_pulses, [], "the phase must not send pulses while the pump is running"
        )
        self.assertEqual(master.waits[0], 41 + 1, "the first wait is the tick-zero pulse")
        self.assertIn(41 + 6 * 28 + 1, master.waits, "the last fixture step waits for its own grid")

    def test_a_master_without_a_pump_keeps_sending_its_own_pulses(self) -> None:
        master = _PumpMaster(origin_pulse=1, pump=False)
        with patch.object(baseline, "_send_short_press"):
            baseline._stream_fixture_record(
                _Port(),
                _Port(),
                fixture=(),
                target_bars=1,
                midi_channel_1based=8,
                master=master,
                abort=_Abort(),
                max_seconds_guard=10,
            )
        self.assertEqual(len(master.sent_pulses), MIDI_CLOCKS_PER_BAR)


class RecordStopTriggerTests(unittest.TestCase):
    """The phase presses its own stop `advance` clocks before the boundary, once."""

    def test_the_press_lands_the_effect_on_the_two_bar_boundary(self) -> None:
        clock = _Clock(index_now=47.0)
        pressed_at: list[int | None] = []
        with patch.object(
            baseline, "_send_short_press", side_effect=lambda *_a, **_kw: pressed_at.append(clock.last_wait)
        ) as stop:
            _notes, clocks, _grids, _stop_grids = _run_with_clock(
                clock, _Port(), grid_origin_index=0, stop_press_advance_clocks=50
            )
        self.assertEqual(clocks, 2 * MIDI_CLOCKS_PER_BAR)
        self.assertEqual(stop.call_count, 1, "exactly one in-stream stop press")
        self.assertEqual(stop.call_args.kwargs["note"], baseline.RECORD_BUTTON_NOTE)
        self.assertEqual(
            pressed_at,
            [2 * MIDI_CLOCKS_PER_BAR - 50],
            "the press is emitted `advance` clocks before the two-bar boundary",
        )

    def test_a_stop_press_inside_the_phases_own_end_is_one_press(self) -> None:
        # The press is emitted `advance` clocks before `target_clocks`, i.e. grid position
        # `origin + (192 - advance)`; the device's own press latency then lands the *effect* on
        # tick 1536 (2 bars), which is the bucket the seed is committed in.
        advance = record_stop_press_advance_clocks(bpm=300.0, press_ms=120)
        self.assertEqual(advance, 50)
        effect_tick = (2 * MIDI_CLOCKS_PER_BAR - advance) * TICKS_PER_CLOCK + round(
            (120 + 300) / 1000.0 * (300.0 / 60.0 * 192)
        )
        self.assertLessEqual(abs(effect_tick - 2 * 768), 384, "inside the two-bar bucket")


class SeedPhaseWiringTests(unittest.TestCase):
    """The seed adapter takes the lead and the take-origin grid, and reports who stopped."""

    def _args(self, **over):
        base = dict(
            press_ms=120,
            stop_press_advance_clocks=0,
            tempo_bpm=300.0,
            state_sync_timeout_ms=8000,
            edit_record_fixture=True,
        )
        base.update(over)
        return SimpleNamespace(**base)

    def test_the_seed_phase_gets_the_button_lead_and_the_take_origin(self) -> None:
        clock = _Clock(index_now=100.0)
        captured: dict = {}

        def _fake_streamer(_out, _in, **kwargs):
            captured.update(kwargs)
            return 8, 2 * MIDI_CLOCKS_PER_BAR

        started = time.monotonic() - 0.5
        with patch(
            "host_midi_automation_edit_baseline._stream_fixture_record", side_effect=_fake_streamer
        ), patch("hitl.legacy_edit_baseline._send_short_press"):
            notes, clocks, ok, stop_sent, _intent = _stream_fixture_record_bars(
                _Port(),
                _Port(),
                None,
                self._args(),
                fixture=EDIT_RECORD_FIXTURE,
                target_bars=2,
                midi_channel_1based=8,
                abort=_Abort(),
                record_schedule_start_mono=started,
                skip_clock_preamble=True,
                clock=clock,
            )
        self.assertTrue(ok)
        self.assertEqual((notes, clocks), (8, 2 * MIDI_CLOCKS_PER_BAR))
        self.assertEqual(
            captured["stop_press_advance_clocks"],
            record_stop_press_advance_clocks(bpm=300.0, press_ms=120),
            "the phase is handed the button's own latency as its press lead",
        )
        # The take origin: `record_schedule_start_mono` is the transport press, processed one button
        # lead later; the reader's grid at that instant is 100 - 0.08s / period = 90 (300 BPM).
        self.assertEqual(captured["grid_origin_index"], 90)
        self.assertTrue(stop_sent, "the phase owns the stop it just pressed")

    def test_a_phase_that_breaks_out_early_still_leaves_the_stop_to_the_caller(self) -> None:
        clock = _Clock(index_now=100.0)

        def _short_streamer(_out, _in, **_kwargs):
            return 8, 30  # clock dropout after 30 of 192 clocks

        with patch(
            "host_midi_automation_edit_baseline._stream_fixture_record", side_effect=_short_streamer
        ), patch("hitl.legacy_edit_baseline._send_short_press"):
            _notes, _clocks, ok, stop_sent, _intent = _stream_fixture_record_bars(
                _Port(),
                _Port(),
                None,
                self._args(),
                fixture=EDIT_RECORD_FIXTURE,
                target_bars=2,
                midi_channel_1based=8,
                abort=_Abort(),
                record_schedule_start_mono=time.monotonic() - 0.5,
                skip_clock_preamble=True,
                clock=clock,
            )
        self.assertTrue(ok)
        self.assertFalse(stop_sent, "nothing was pressed, so the caller must stop the take")


if __name__ == "__main__":
    unittest.main()
