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
from hitl.transport_clock import stream_pattern_for_bars  # noqa: E402

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

    def test_first_poll_after_a_long_wait_does_not_poison_the_tempo(self) -> None:
        # Device failure this pins (`captures/host_midi_automation_baseline_20260930_121454`): the
        # reader is first polled only after the transport-start settlement, so its first interval
        # was that ~0.5 s gap. Taking it as the median reported "0.0 BPM" with 251 pulses seen and
        # froze the phase, so the streamer's pulse waits all timed out.
        virtual = VirtualClock()
        port = PulsePort(virtual, period=PERIOD_120_BPM)
        reader = MidiClockReader(port, clock=virtual)
        reader.poll()                      # the pulse that sets the anchor
        port.next_pulse_at += 0.5          # the settlement gap, with no pulses in it
        virtual.t = port.next_pulse_at
        reader.poll()                      # the pulse after the gap
        run_for(reader, virtual, 6.0, port)
        # The 0.5 s gap must not become the tempo: the stream after it is a clean 120 BPM.
        self.assertAlmostEqual(reader.bpm() or 0, 120.0, delta=0.05)
        # And the phase tracks the real grid, not the number of drained pulses.
        self.assertAlmostEqual(reader.index_now(), 6.0 / PERIOD_120_BPM, delta=2.0)

    def test_clock_evidence_is_recency_not_history(self) -> None:
        # The guards ask this reader "is a clock flowing *now*?" to decide whether the transport
        # needs starting. `pulse_seen()` is sticky, so using it kept answering True after the
        # record-arm step stopped the transport: the record leg then ran with 0 notes captured
        # and a 1-bar take. The answer must expire when the pulses stop.
        virtual = VirtualClock()
        port = PulsePort(virtual, period=PERIOD_120_BPM)
        reader = MidiClockReader(port, clock=virtual)
        run_for(reader, virtual, 2.0, port)
        self.assertTrue(reader.pulse_seen())
        self.assertTrue(reader.recent_pulse())
        self.assertLess(reader.last_pulse_age_s() or 99.0, 0.05)
        # The device stops emitting clock when its transport stops.
        virtual.t += 1.0
        self.assertFalse(reader.recent_pulse())
        self.assertGreater(reader.last_pulse_age_s() or 0.0, 0.9)
        # ...and it comes back as soon as the clock does.
        run_for(reader, virtual, 0.2, port)
        self.assertTrue(reader.recent_pulse())

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



class TickingClock:
    """Virtual clock that advances on every read, and a helper that lets it run pulse by pulse.

    Clock pacing is asserted in *virtual* seconds and in received-pulse counts, never against wall
    time. The reader's phase-locked index is rebuilt from pulse *arrival intervals*, so a virtual
    clock may not hand a burst of pulses to one drain (that mis-measures the interval and the index
    then races): `run_pulses` advances one tick at a time and polls the way a phase does.
    """

    def __init__(self, *, step: float) -> None:
        self.t = 1000.0
        self.step = step

    def __call__(self) -> float:
        self.t += self.step
        return self.t


# Ticks of `TickingClock` per MIDI pulse: fine enough that a pulse is drained on its own.
TICKS_PER_PULSE = 40


def run_pulses(reader: MidiClockReader, ticker: TickingClock, pulses: int) -> None:
    """Let ``pulses`` pulses arrive on the virtual clock, one drain at a time."""
    for _ in range(pulses * TICKS_PER_PULSE):
        reader.poll()
        ticker()


class GridRecordingPort(PulsePort):
    """PulsePort that logs, for every outgoing message, the reader's grid position at send time."""

    def __init__(self, virtual: VirtualClock, *, period: float, reader: MidiClockReader) -> None:
        super().__init__(virtual, period=period)
        self.reader = reader
        self.log: list[tuple[str, object, float, int]] = []

    def send(self, message) -> None:
        self.log.append(
            (message.type, getattr(message, "note", None), self.virtual.t,
             self.reader.observed_pulses())
        )
        super().send(message)


def device_paced_ports(
    *, warmup_pulses: int = 24, dropout_pulses: int = 0, dropout_at: int = 0
) -> tuple[TickingClock, PulsePort, MidiClockReader, GridRecordingPort, int, float]:
    """A reader on a device clock that has been running, plus a port that logs its grid.

    Returns ``(ticker, in_port, reader, out_port, pulses_at_start, phase_start)``: the phase in the
    tests below starts exactly where a real record/overdub phase starts -- on a live grid.
    """
    ticker = TickingClock(step=PERIOD_120_BPM / TICKS_PER_PULSE)
    in_port = PulsePort(
        ticker,
        period=PERIOD_120_BPM,
        dropout_pulses=dropout_pulses,
        dropout_at=dropout_at,
    )
    reader = MidiClockReader(in_port, clock=ticker)
    run_pulses(reader, ticker, warmup_pulses)
    return (
        ticker,
        in_port,
        reader,
        GridRecordingPort(ticker, period=PERIOD_120_BPM, reader=reader),
        reader.observed_pulses(),
        ticker.t,
    )


class StreamerGridPacingTests(unittest.TestCase):
    """`stream_pattern_for_bars` paced from a clock instead of the wall clock.

    Two properties, both from the 2026-09-30 requirement (evidence
    `captures/host_midi_automation_baseline_20260930_113640.json`: 15 clocks to the first note, a
    2-bar request recorded as 3 bars):

    1. the first grid step of a phase is emitted on the *next* pulse of that phase, so the first
       note is one pulse after the phase start instead of ~320 ms (several clocks) later;
    2. a phase ends at `target_bars * MIDI_CLOCKS_PER_BAR` grid positions -- counted in grid
       positions, not in wall-clock seconds.
    """

    @staticmethod
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

    def test_device_paced_phase_starts_on_the_next_pulse(self) -> None:
        _ticker, in_port, reader, out_port, pulses_at_start, phase_start = device_paced_ports()
        # A live grid: half a second of pulses in, so the phase origin is a mid-stream grid
        # position rather than zero (the first pulse is drained before the first tick, hence 25).
        self.assertGreaterEqual(pulses_at_start, 24)

        _notes, _cc, clocks, timing = stream_pattern_for_bars(
            out_port, in_port, clock=reader, **self._kwargs()
        )

        self.assertEqual(timing["clock_source"], "device_clock")
        self.assertEqual(clocks, 2 * MIDI_CLOCKS_PER_BAR)
        self.assertEqual(int(timing["clock_pulses_counted"]), 2 * MIDI_CLOCKS_PER_BAR)
        note_ons = [row for row in out_port.log if row[0] == "note_on"]
        # 2 bars of 16ths (step_clocks=6), the first one on the phase's first pulse.
        self.assertEqual(len(note_ons), 32)
        self.assertEqual(note_ons[0][3], pulses_at_start + 1, "first note is on the next pulse")
        self.assertLess(
            note_ons[0][2] - phase_start,
            3 * PERIOD_120_BPM,
            "the first note must not wait several clocks (the old ~320 ms phase delay)",
        )
        # Every later step is on the grid too. The grid is the reader's *phase-locked* index, not a
        # raw message count: it is rebuilt from pulse arrival times, so a step may sit one pulse
        # either side of the raw count (`observed_pulses` is evidence, not the grid -- the whole
        # point of the phase-locked index is that a dropout must not stall it).
        for index, row in enumerate(note_ons):
            expected_pulse = pulses_at_start + 1 + index * 6
            self.assertLessEqual(
                abs(row[3] - expected_pulse), 1, f"step {index} belongs to pulse {expected_pulse}"
            )

    def test_device_paced_phase_survives_a_dropped_pulse_burst(self) -> None:
        _ticker, in_port, reader, out_port, pulses_at_start, _phase_start = device_paced_ports(
            dropout_pulses=24, dropout_at=96
        )

        _notes, _cc, clocks, timing = stream_pattern_for_bars(
            out_port, in_port, clock=reader, **self._kwargs()
        )

        # The phase still spans its full grid: a raw pulse count would have run a quarter bar
        # short here and the take with it.
        self.assertEqual(clocks, 2 * MIDI_CLOCKS_PER_BAR)
        self.assertEqual(int(timing["clock_pulses_counted"]), 2 * MIDI_CLOCKS_PER_BAR)
        self.assertLess(
            reader.observed_pulses() - pulses_at_start, 2 * MIDI_CLOCKS_PER_BAR,
            "a quarter bar of pulses was lost, so the raw count runs short of the grid",
        )
        self.assertGreaterEqual(reader.gap_count(), 1)

    def test_host_paced_phase_sends_exactly_the_requested_pulses(self) -> None:
        virtual = VirtualClock()
        in_port = PollOnlyPort(virtual, period=PERIOD_120_BPM)
        out_port = PulsePort(virtual, period=PERIOD_120_BPM)
        master = MidiClockMaster(out_port)

        _notes, _cc, clocks, timing = stream_pattern_for_bars(
            out_port, in_port, master=master, wall_clock_tempo_bpm=6000.0, **self._kwargs()
        )

        self.assertEqual(timing["clock_source"], "host_clock")
        self.assertEqual(clocks, 2 * MIDI_CLOCKS_PER_BAR)
        kinds = [message.type for message in out_port.sent]
        self.assertEqual(kinds.count("clock"), 2 * MIDI_CLOCKS_PER_BAR)
        # The caller owns Start/Stop: the streamer must not add a realtime transport message.
        self.assertNotIn("start", kinds)
        self.assertNotIn("stop", kinds)
        first_note = kinds.index("note_on")
        # The first pulse IS the downbeat: exactly one clock precedes the first note, and step k is
        # emitted with pulse 1 + (k - 1) * 6.
        self.assertEqual(kinds[:first_note].count("clock"), 1)
        self.assertEqual(kinds[:first_note].count("clock"), 1)
        second_note = kinds.index("note_on", first_note + 1)
        self.assertEqual(kinds[:second_note].count("clock"), 1 + 6, "step k belongs to pulse k")


if __name__ == "__main__":
    unittest.main(verbosity=2)
