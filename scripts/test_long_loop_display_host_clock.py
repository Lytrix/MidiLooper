#!/usr/bin/env python3
"""Host tests for the long-loop leg's `--clock-source {device,host}` (task 3.7g).

The leg records a long take and then reads the display/rolling evidence from it, so its *timing* was
always the device's own transport at 120 BPM: it pressed play/stop, and the stream was paced by the
wall-clock proxy. Evaluation of the two-bar window then had to infer the grid from the device's
clock, and a window boundary could only be named in seconds.

`--clock-source host` makes the evaluation's clock explicit: the host sends 24 PPQN at
`--tempo-bpm`, its realtime `Start` is the device's tick 0 (`ClockManager::onMidiStart`), and the
leg sends **no transport press** (a press is a toggle and would stop the run the Start begins, and
an inbound-clock wait can only fail because a slaved device emits no clock). The shape is the one
`legacy_record_baseline.py` already uses; what is new here is that this leg takes it.

What these tests can pin without the bench:

* the flags exist, default to the device's own clock, and reach the leg's namespace;
* the guard's bar length follows the tempo the host actually sends in host mode, and keeps the
  measured 2 s bar in device mode (the default must change nothing);
* the wiring is still there -- the master is created per run, started where the transport used to be
  pressed (and again after the clear), paces the stream as its only pacing source, and is retired
  with `stop_pump()` rather than a `Stop`, because the wrapper's switch leg needs the transport
  running and the device falls back to its own clock after its 500 ms slave timeout.

The behaviour itself -- a 32-bar take at `len % TICKS_PER_BAR == 0` under a host clock -- is the
device read, recorded in `openspec/changes/windowed-playback-generation/evidence/`.

Run: .venv/bin/python scripts/test_long_loop_display_host_clock.py
"""
from __future__ import annotations

import inspect
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.control_constants import TICKS_PER_BAR  # noqa: E402
from hitl.scenarios.long_loop_display_window import (  # noqa: E402
    _host_stop_lead_clocks,
    _parse_common_args,
    _seconds_per_bar,
)


def _parse(legacy_args: list[str]) -> SimpleNamespace:
    return _parse_common_args(SimpleNamespace(legacy_args=legacy_args))


class LongLoopClockSourceArgumentTests(unittest.TestCase):
    """The flags are the leg's contract with the runner, which forwards unknown arguments verbatim."""

    def test_the_default_is_the_device_clock_at_120(self) -> None:
        ns = _parse([])
        self.assertEqual(ns.clock_source, "device")
        self.assertEqual(ns.tempo_bpm, 120.0)

    def test_the_host_clock_and_its_tempo_reach_the_leg(self) -> None:
        ns = _parse(["--clock-source", "host", "--tempo-bpm", "90"])
        self.assertEqual(ns.clock_source, "host")
        self.assertEqual(ns.tempo_bpm, 90.0)

    def test_the_device_clock_can_be_named_explicitly(self) -> None:
        ns = _parse(["--clock-source", "device", "--tempo-bpm", "120"])
        self.assertEqual(ns.clock_source, "device")

    def test_an_unknown_clock_source_is_refused_by_the_parser(self) -> None:
        with self.assertRaises(SystemExit):
            _parse(["--clock-source", "external"])

    def test_a_non_positive_tempo_is_the_legs_own_refusal(self) -> None:
        # The parser must accept it so the leg can name it on stdout before opening a port; the leg's
        # `if ns.tempo_bpm <= 0:` return 2 is the gate (pinned by the wiring scan below).
        ns = _parse(["--tempo-bpm", "0"])
        self.assertEqual(ns.tempo_bpm, 0.0)


class LongLoopBarLengthTests(unittest.TestCase):
    """The record phase's guard is sized in bars, so a host tempo must reach it."""

    def test_device_mode_keeps_the_measured_two_second_bar(self) -> None:
        for tempo in (60.0, 90.0, 120.0, 300.0):
            self.assertEqual(_seconds_per_bar("device", tempo), 2.0)

    def test_host_mode_follows_the_tempo_the_host_sends(self) -> None:
        self.assertAlmostEqual(_seconds_per_bar("host", 120.0), 2.0)
        self.assertAlmostEqual(_seconds_per_bar("host", 90.0), (60.0 / 90.0) * 4.0)
        self.assertAlmostEqual(_seconds_per_bar("host", 300.0), (60.0 / 300.0) * 4.0)

    def test_the_guard_scales_with_the_slow_host(self) -> None:
        # 32 bars at 90 BPM is 85.3 s; a guard left at the device's 2 s/bar would be 60 s and cut the
        # take off mid-stream. The streamer's guard is `record_bars * seconds_per_bar * 2.5` with a
        # 30 s floor, which is what this pins.
        record_bars = 32
        slow = max(30.0, record_bars * _seconds_per_bar("host", 90.0) * 2.5)
        device = max(30.0, record_bars * _seconds_per_bar("device", 90.0) * 2.5)
        self.assertGreater(slow, record_bars * (60.0 / 90.0) * 4.0)
        self.assertGreater(slow, device)


class LongLoopStopLeadTests(unittest.TestCase):
    """The host-mode stop press is sent from inside the stream, a lead ahead of the last bar."""

    def test_the_lead_follows_the_tempo_in_clocks(self) -> None:
        self.assertEqual(_host_stop_lead_clocks(90.0), 32)
        # 120 BPM's lateness (43 clocks) is capped at 0.4 bar so the press still closes the take's last bar.
        self.assertEqual(_host_stop_lead_clocks(120.0), 38)

    def test_the_lead_is_positive_at_any_usable_tempo(self) -> None:
        for tempo in (20.0, 60.0, 90.0, 120.0, 300.0):
            self.assertGreaterEqual(_host_stop_lead_clocks(tempo), 1)

    def test_the_lead_stays_inside_the_last_bar(self) -> None:
        # The commit is the raw stop tick quantised to the nearest bar, so the press may be up to half a bar early
        # or late. One second of lead at the slowest tempo the leg is used at must still be inside that window.
        for tempo in (60.0, 90.0, 120.0, 300.0):
            half_bar_clocks = 96 / 2  # clocks per bar is tempo-independent (24 PPQN, 4-4)
            self.assertLess(_host_stop_lead_clocks(tempo), half_bar_clocks)


class LongLoopClockWiringTests(unittest.TestCase):
    """A tripwire for the call sites, which no unit can drive without the bench.

    The decisions above are pinned directly; this adds that the leg still routes through them. It is a
    source scan in the spirit of the repo's compatibility-inventory gates: it fails if the wiring is
    removed or renamed, and it says nothing about behaviour (the device read is what proves it).
    """

    def setUp(self) -> None:
        from hitl.scenarios import long_loop_display_window as module

        self.source = inspect.getsource(module)

    def test_the_master_is_created_per_run_from_the_flag(self) -> None:
        self.assertIn(
            "MidiClockMaster(out_port) if ns.clock_source == \"host\" else None", self.source
        )

    def test_the_host_start_replaces_the_transport_press(self) -> None:
        # Host mode sends exactly one Start, and it is the one after the record arm: that Start is the device's
        # tick 0, so starting earlier (as the first host-clock run did) puts the take's first bar ahead of the
        # stream's anchor and commits one bar too many (`captures/i5-s37g-host-clock-01`: 25344 ticks).
        self.assertEqual(self.source.count("clock_master.start(bpm=ns.tempo_bpm, abort=abort)"), 1)
        arm = self.source.index("record_baseline_len = len(serial_collector.snapshot())")
        start = self.source.index("clock_master.start(bpm=ns.tempo_bpm, abort=abort)")
        stream = self.source.index("record stream notes=")
        self.assertLess(arm, start)
        self.assertLess(start, stream)
        # The post-boot/post-STOPPED clock recovery is a press; in host mode it cannot run.
        self.assertIn("if ns.start_transport and clock_master is None:", self.source)

    def test_the_stream_counts_its_bars_from_the_takes_own_origin(self) -> None:        # Without this the stream's bar budget starts whenever the process began streaming, which is what added a
        # bar; with it the budget is measured from tick 0 and a late start only costs notes at the pattern's head.
        self.assertIn("grid_origin_pulse=record_grid_origin_pulse,", self.source)
        self.assertIn("record_grid_origin_pulse = clock_master.grid_origin_pulse()", self.source)
        self.assertIn("grid_anchor_missed_pulses", self.source)

    def test_the_clear_stops_the_host_transport_with_a_realtime_stop(self) -> None:
        self.assertIn("clock_master.stop()", self.source)

    def test_the_stream_is_paced_by_exactly_one_source(self) -> None:
        self.assertIn("master=clock_master,", self.source)
        self.assertIn("wall_clock_tempo_bpm=wall_clock_tempo,", self.source)
        # Host mode has no inbound clock to wait for and no proxy to resolve.
        self.assertIn("clock_ok, using_serial_proxy = True, False", self.source)
        self.assertIn("wall_clock_tempo = None", self.source)

    def test_teardown_retires_the_pump_without_stopping_the_take(self) -> None:
        self.assertIn("clock_master.stop_pump()", self.source)
        self.assertNotIn("clock_master.stop()\n        out_port.close()", self.source)

    def test_the_report_names_the_clock_the_run_used(self) -> None:
        self.assertIn('"clock_source": ns.clock_source,', self.source)
        self.assertIn('"tempo_bpm": ns.tempo_bpm,', self.source)

    def test_the_tempo_is_refused_before_any_take(self) -> None:
        self.assertIn("if ns.tempo_bpm <= 0:", self.source)

    def test_host_mode_presses_the_stop_from_inside_the_stream(self) -> None:
        # The leg's own press comes ~0.9 s late (0.5 s baseline wait + the device's double-tap window), which at a
        # slow host tempo is outside the bar's half-bar quantum. Host mode hands the press to the streamer instead,
        # and the leg then only watches for the transition -- a second press would arm a new take.
        self.assertIn(
            "stop_press_note=None if clock_master is None else RECORD_BUTTON_NOTE,", self.source
        )
        self.assertIn("host_stop_lead_clocks = _host_stop_lead_clocks(ns.tempo_bpm)", self.source)
        self.assertIn("if stop_press_sent_during_stream:", self.source)

    def test_the_tick_grid_the_read_is_judged_on_is_the_firmwares(self) -> None:
        # The acceptance is `len % TICKS_PER_BAR == 0`; the constant the leg's world shares with the
        # firmware is what the device read compares against (24 PPQN, 4-4 -> 96 clocks per bar, 8 ticks
        # per clock). Pinned here so a host-side edit cannot quietly move the bar.
        self.assertEqual(TICKS_PER_BAR, 768)


if __name__ == "__main__":
    unittest.main(verbosity=2)
