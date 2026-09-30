#!/usr/bin/env python3
"""Host tests for the HITL MIDI clock contract (`hitl/midi_clock.py`).

Four properties are pinned without a device:

1. **Tempo is learned, not assumed.** A clean 24 PPQN stream at 120 BPM must read back as
   120 BPM inside 0.05, so the host can stop guessing `--tempo-bpm`.
2. **A dropped pulse must not stall the phase.** The device's clock drops out on a busy run;
   a raw pulse counter then sits a quarter beat behind the device's bar grid forever. The
   phase-locked index must stay linear in time and report the gap.
3. **A burst must not double-count.** After the host is descheduled, several pulses arrive in
   one drain. Time has already moved the phase past them, so they must not push the bar
   boundary early (`captures/midi-clock-probe`: bar 3 landed 121 ms early when they did).
4. **A stopped device must time out, not hang.** `wait_pulse` is the phase-length primitive,
   so its failure mode has to be a bounded False.

The master side pins absolute-deadline pacing and the device-side echo probe.

Run: .venv/bin/python scripts/test_midi_clock.py
"""
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hitl.control_constants import MIDI_CLOCKS_PER_BAR  # noqa: E402
from hitl.midi_clock import MidiClockMaster, MidiClockReader  # noqa: E402

PERIOD_120_BPM = 60.0 / 120.0 / 24.0  # 20.8333 ms == 24 PPQN at 120 BPM


class VirtualClock:
    """Injectable monotonic clock, so the timing checks are deterministic and instant."""

    def __init__(self) -> None:
        self.t = 1000.0

    def __call__(self) -> float:
        return self.t


def clock_message():
    return type("Msg", (), {"type": "clock", "note": None})()


class PulsePort:
    """Emits one clock pulse per virtual period, with an optional dropout window."""

    def __init__(self, virtual: VirtualClock, *, period: float, dropout_pulses: int = 0,
                 dropout_at: int = 0, burst_at: int = 0, burst_pulses: int = 0) -> None:
        self.virtual = virtual
        self.period = period
        self.next_pulse_at = virtual.t
        self.emitted = 0
        self.dropout_pulses = dropout_pulses
        self.dropout_at = dropout_at
        self.burst_at = burst_at
        self.burst_pulses = burst_pulses
        self.sent: list = []

    def iter_pending(self):
        while self.virtual.t >= self.next_pulse_at:
            index = self.emitted
            self.emitted += 1
            self.next_pulse_at += self.period
            if self.dropout_pulses and self.dropout_at <= index < self.dropout_at + self.dropout_pulses:
                continue  # the pulse is lost, but the device grid keeps moving
            yield clock_message()
            if self.burst_pulses and index == self.burst_at:
                # The same drain hands over several pulses that were already due.
                for _ in range(self.burst_pulses - 1):
                    yield clock_message()

    def poll(self):
        for message in self.iter_pending():
            return message
        return None

    def send(self, message) -> None:
        self.sent.append(message)


class PollOnlyPort:
    """The shape the existing clock tests use: `poll()` and `send()` only."""

    def __init__(self, virtual: VirtualClock, *, period: float) -> None:
        self.virtual = virtual
        self.period = period
        self.next_pulse_at = virtual.t
        self.sent: list = []

    def poll(self):
        if self.virtual.t >= self.next_pulse_at:
            self.next_pulse_at += self.period
            return clock_message()
        return None

    def send(self, message) -> None:
        self.sent.append(message)


def run_for(reader: MidiClockReader, virtual: VirtualClock, seconds: float, port=None) -> None:
    """Advance virtual time to exact pulse boundaries so intervals are not step-quantized."""
    end = virtual.t + seconds
    while virtual.t < end - 1e-9:
        target = end
        if port is not None and port.next_pulse_at < end:
            target = max(virtual.t + 1e-9, port.next_pulse_at)
        virtual.t = target
        reader.poll()


class ReaderTests(unittest.TestCase):
    def test_clean_clock_reads_the_device_tempo_and_the_bar_grid(self) -> None:
        virtual = VirtualClock()
        port = PulsePort(virtual, period=PERIOD_120_BPM)
        reader = MidiClockReader(port, clock=virtual)
        run_for(reader, virtual, 8.0, port)
        self.assertAlmostEqual(reader.bpm() or 0, 120.0, delta=0.05)
        self.assertAlmostEqual(reader.stats()["jitter_ms"], 0.0, delta=1e-6)
        self.assertEqual(reader.gap_count(), 0)
        # 8 s at 120 BPM is 16 bars; raw count and phase must agree exactly when nothing is lost.
        self.assertAlmostEqual(reader.observed_pulses(), 8.0 / PERIOD_120_BPM, delta=1)
        self.assertAlmostEqual(reader.index_now(), 8.0 / PERIOD_120_BPM, delta=1)

    def test_dropped_pulses_do_not_stall_the_phase(self) -> None:
        virtual = VirtualClock()
        port = PulsePort(virtual, period=PERIOD_120_BPM, dropout_pulses=24, dropout_at=96)
        reader = MidiClockReader(port, clock=virtual)
        run_for(reader, virtual, 10.0, port)
        # 24 pulses were lost, so the raw count is one quarter of a bar short of the phase.
        self.assertAlmostEqual(reader.observed_pulses(), 10.0 / PERIOD_120_BPM - 23, delta=2)
        self.assertAlmostEqual(reader.index_now(), 10.0 / PERIOD_120_BPM, delta=1.5)
        self.assertEqual(reader.gap_count(), 1)
        self.assertAlmostEqual(reader.bpm() or 0, 120.0, delta=0.05)

    def test_a_burst_after_a_host_stall_does_not_double_count(self) -> None:
        virtual = VirtualClock()
        # The host was descheduled past two pulses, so three arrive in one drain.
        port = PulsePort(virtual, period=PERIOD_120_BPM, burst_at=96, burst_pulses=3)
        reader = MidiClockReader(port, clock=virtual)
        run_for(reader, virtual, 10.0, port)
        # Time, not the message count, decides the grid: the phase must not run ahead of it.
        self.assertAlmostEqual(reader.index_now(), 10.0 / PERIOD_120_BPM, delta=1.5)
        self.assertAlmostEqual(reader.bpm() or 0, 120.0, delta=0.05)

    def test_stall_and_burst_together(self) -> None:
        virtual = VirtualClock()
        port = PulsePort(virtual, period=PERIOD_120_BPM, dropout_pulses=24, dropout_at=96,
                         burst_at=200, burst_pulses=4)
        reader = MidiClockReader(port, clock=virtual)
        run_for(reader, virtual, 12.0, port)
        self.assertAlmostEqual(reader.index_now(), 12.0 / PERIOD_120_BPM, delta=2.0)
        self.assertGreaterEqual(reader.gap_count(), 1)
        self.assertAlmostEqual(reader.bpm() or 0, 120.0, delta=0.05)

    def test_poll_only_port_is_drained_bulk_style(self) -> None:
        # The existing clock fixtures expose only `poll()`; those ports must still be drained
        # to the end of their backlog and every pulse counted. A frozen virtual clock carries
        # no interval information, so this check pins the raw count and not the phase.
        virtual = VirtualClock()
        port = PollOnlyPort(virtual, period=PERIOD_120_BPM)
        reader = MidiClockReader(port, clock=virtual)
        run_for(reader, virtual, 4.0)
        self.assertAlmostEqual(reader.observed_pulses(), 4.0 / PERIOD_120_BPM, delta=1)
        self.assertIsNone(reader.bpm())

    def test_wait_bars_lands_on_the_grid_and_times_out_on_a_stopped_device(self) -> None:
        virtual = VirtualClock()
        port = PulsePort(virtual, period=PERIOD_120_BPM)

        def ticker() -> float:
            virtual.t += 0.004
            return virtual.t

        reader = MidiClockReader(port, clock=ticker)
        started_at = virtual.t
        self.assertTrue(reader.wait_bars(1, timeout_s=99.0))
        # The phase, not the received-pulse count, is the target: it must land on the device
        # bar grid inside one pulse period (the wait is what a bar-length phase is built on).
        self.assertAlmostEqual(
            virtual.t - started_at, PERIOD_120_BPM * MIDI_CLOCKS_PER_BAR, delta=PERIOD_120_BPM
        )

        # A stopped device must time out, not hang: the clock keeps running, the pulses do not.
        class Advancing:
            def __init__(self, t: float) -> None:
                self.t = t

            def __call__(self) -> float:
                self.t += 0.01
                return self.t

        stopped = PulsePort(VirtualClock(), period=PERIOD_120_BPM)
        stopped.next_pulse_at = 10_000.0
        reader2 = MidiClockReader(stopped, clock=Advancing(virtual.t))
        wall = time.monotonic()
        self.assertFalse(reader2.wait_pulse(1, timeout_s=0.05))
        self.assertLess(time.monotonic() - wall, 3.0)

    def test_device_realtime_transport_is_observed(self) -> None:
        virtual = VirtualClock()
        port = PulsePort(virtual, period=PERIOD_120_BPM)
        reader = MidiClockReader(port, clock=virtual)
        reader.poll()
        # The device announces its own start with the downbeat clock (toggleTransport -> sendStart).
        start = type("Msg", (), {"type": "start", "note": None})()
        original = port.iter_pending
        port.iter_pending = lambda: iter([start])
        reader.poll()
        port.iter_pending = original
        self.assertEqual(reader.transport_messages(), ["start"])


class MasterTests(unittest.TestCase):
    def test_bars_pace_without_accumulating_drift(self) -> None:
        port = PulsePort(VirtualClock(), period=0.02)
        master = MidiClockMaster(port)
        report = master.bars(2, bpm=6000.0)  # 6000 BPM -> 4.17 ms period, a fast but valid run
        self.assertEqual(int(report["pulses_sent"]), 2 * MIDI_CLOCKS_PER_BAR)
        self.assertEqual(len(port.sent), 2 * MIDI_CLOCKS_PER_BAR)
        # Absolute deadlines: the tail must not carry an accumulated per-step error.
        one_period_ms = 60.0 / 6000.0 / 24.0 * 1e3
        self.assertLess(abs(report["drift_ms"]), one_period_ms * 8)
        self.assertLess(abs(report["max_err_ms"]), one_period_ms * 8)

    def test_start_and_stop_are_explicit(self) -> None:
        port = PulsePort(VirtualClock(), period=0.02)
        master = MidiClockMaster(port)
        master.start()
        master.stop()
        self.assertEqual([m.type for m in port.sent], ["start", "stop"])

    def test_drive_reports_the_device_echo_it_saw(self) -> None:
        # A device that is slaving still emits for the transition window, so the report must
        # separate the settle quarter from the tail.
        virtual = VirtualClock()
        echo_port = PulsePort(virtual, period=PERIOD_120_BPM)

        def first_then_quiet():
            # One pulse at the start of the drive, then nothing.
            if first_then_quiet.done:
                return iter(())
            first_then_quiet.done = True
            return iter([clock_message()])

        first_then_quiet.done = False
        echo_port.iter_pending = first_then_quiet
        echo_reader = MidiClockReader(echo_port, clock=virtual)
        out_port = PulsePort(virtual, period=0.02)
        master = MidiClockMaster(out_port)
        report = master.bars(1, bpm=6000.0, echo_reader=echo_reader)
        self.assertEqual(int(report["device_clock_echo_pulses"]), 1)
        self.assertEqual(int(report["device_clock_echo_tail_pulses"]), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
