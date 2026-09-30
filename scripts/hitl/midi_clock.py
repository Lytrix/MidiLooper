"""Host-side MIDI clock for HITL: read the device's clock, or drive the device's clock.

The premise this module replaces was wrong: the board **does** emit USB MIDI clock on its
device port. Measured on device (2026-09-30, ``teensy41-capture-serial`` family): 48.00 Hz
= exactly 24 PPQN at 120.00 BPM, 384/384 pulses per 8 s window, non-gap interval jitter
0.65 ms stdev, with occasional short dropouts on a busy run. The clock is not emitted while
the transport is stopped, so a readiness probe that waits for clock *before* starting the
transport can only ever report "no clock".

Read (device is master -- default, and it needs no device state change)
---------------------------------------------------------------------
:class:`MidiClockReader` drains its input port in bulk, keeps only realtime messages, and
turns the device clock into a monotonic *pulse index*:

* :meth:`MidiClockReader.bpm` is a rolling median of the pulse intervals, so the host learns
  the device's real tempo instead of assuming ``--tempo-bpm``. The serial fallback cannot do
  this: ``SC_BPM`` (the only emitter of ``#CAP,BPM``) is called only from
  ``ClockManager::onMidiClockPulse``, i.e. only while the device *receives* external clock,
  and ``SESSION_CAPTURE_BPM`` defaults to 0, so the mandated build drops the family at the
  capture sink.
* :meth:`MidiClockReader.index_now` is a **phase-locked** index, not a counter: it advances
  on a monotonic clock and every received pulse re-anchors it to the nearest grid position.
  That is what makes it survive the two shapes a raw pulse count cannot: a dropout (the phase
  keeps moving with time instead of stalling a quarter beat behind the device's bar grid) and
  a burst after the host was descheduled (the extra pulses do not double-count an index that
  time has already advanced past). :meth:`observed_pulses` keeps the raw count for evidence,
  and :meth:`gap_count` records how often a pulse was late enough to imply lost grid
  positions.
* :meth:`MidiClockReader.wait_pulse` blocks until pulse *k* is reached on that phase. One bar
  is pulse ``MIDI_CLOCKS_PER_BAR``; a 16th note is ``MIDI_CLOCKS_PER_BEAT / 4``.

Drive (host is master -- pinned tempo)
-------------------------------------
:class:`MidiClockMaster` sends 24 PPQN clock against absolute deadlines with a spin-wait
tail, so per-pulse jitter does not accumulate. Measured on device: 2.9 ms mean / 12.5 ms max
per-pulse error, 2.6 ms cumulative drift over 384 pulses. Three behaviours matter:

* ``Start``/``Stop`` are explicit MIDI realtime messages; the device slaves on the first
  clock pulse and reverts to its own clock on the 500 ms timeout
  (``ClockManager::midiClockTimeout``), which ``ClockManager::checkClockSource`` applies from
  ``loop()`` -- measured resume latency 0.53 s.
* While the device is slaved it emits **no** clock of its own (``updateInternalClock``
  returns early for ``CLOCK_EXTERNAL``), which is the observable proof that it followed.
* Never send a bare ``Start``/``Continue`` without following it with clock: that alone puts
  the device in ``CLOCK_EXTERNAL``, where it emits no clock (measured: the device clock
  collapsed to 0.75 Hz).
"""

from __future__ import annotations

import statistics
import time
from typing import Any, Optional

import mido

from hitl.control_constants import MIDI_CLOCKS_PER_BAR, MIDI_CLOCKS_PER_BEAT

# A pulse that arrives more than this many grid positions late implies lost pulses.
GAP_STEPS = 2
# Tempo validity band, matching the band `ClockManager::onMidiClockPulse` accepts.
# Tempo validity band, matching the band `ClockManager::onMidiClockPulse` accepts, expressed as
# the pulse interval it implies (8.33 ms at 300 BPM .. 125 ms at 20 BPM).
MIN_BPM = 20.0
MAX_BPM = 300.0
MIN_PERIOD_S = 60.0 / MAX_BPM / MIDI_CLOCKS_PER_BEAT
MAX_PERIOD_S = 60.0 / MIN_BPM / MIDI_CLOCKS_PER_BEAT
# Rolling window for the interval median (one quarter note at 24 PPQN).
BPM_WINDOW = MIDI_CLOCKS_PER_BEAT
# Pulses spanned before a drain-level tempo sample is trusted (half a beat at 24 PPQN).
TEMPO_WINDOW_PULSES = MIDI_CLOCKS_PER_BEAT // 2
# Intervals outside this factor of the running median are a stall or a burst, not a sample.
INTERVAL_BAND = (0.5, 2.0)
REALTIME_TRANSPORT_TYPES = ("start", "stop", "continue")


class MidiClockReader:
    """Bulk-drained, phase-locked reader of an incoming USB MIDI clock."""

    def __init__(self, in_port: mido.ports.BaseInput, *, clock: Any = time.monotonic) -> None:
        self._in = in_port
        self._clock = clock
        self._intervals: list[float] = []
        self._anchor_time: Optional[float] = None
        self._anchor_index = 0.0
        self._pulses = 0
        self._gaps = 0
        self._started: Optional[float] = None
        self._transport: list[str] = []
        self._tempo_anchor_first: Optional[float] = None
        self._tempo_anchor_pulses = 0

    # ---- incremental drain -------------------------------------------------
    def _drain(self) -> Any:
        """Yield pending messages, with or without the port's bulk iterator.

        The HITL clock helpers are unit-tested against fake ports that expose only
        ``poll()``, so the bulk path is used when it exists and the single-message path
        otherwise.
        """
        iterator = getattr(self._in, "iter_pending", None)
        if iterator is not None:
            yield from iterator()
            return
        while True:
            message = self._in.poll()
            if message is None:
                return
            yield message

    def poll(self) -> int:
        """Drain every pending message; return the number of clock pulses taken.

        A caller that polls in batches (every harness does: serial work sits between reads)
        receives a whole burst at one instant, so the pulses inside a batch share a timestamp
        and no per-pulse interval is measurable. The tempo is therefore sampled at the **drain**
        level: the gap between consecutive batches, divided by the pulses the earlier batch
        carried. Without that this reader reported "0.0 BPM" with 96 pulses seen in a managed
        run and the streamer's pulse waits all expired.
        """
        taken = 0
        batch_first: Optional[float] = None
        for message in self._drain():
            if message.type == "clock":
                now = self._clock()
                if batch_first is None:
                    batch_first = now
                self._on_pulse(now)
                taken += 1
            elif message.type in REALTIME_TRANSPORT_TYPES:
                self._transport.append(message.type)
        if taken and batch_first is not None:
            self._sample_drain_tempo(batch_first, taken)
        return taken

    def _sample_drain_tempo(self, batch_first: float, batch_count: int) -> None:
        """Feed one tempo sample from the cadence between drains (see :meth:`poll`).

        Cumulative over a fixed window rather than per drain: callers poll on their own cadence (a
        guard may drain mid-gap, then the next gap carries the remaining pulses), so a per-batch
        ``gap / previous_batch_count`` is biased -- it reported 41.5 BPM for a 120 BPM device,
        which then paced a whole record phase at the wrong tempo. Accumulating pulses across
        drains until half a beat is spanned makes the estimate independent of the cadence.
        """
        if self._tempo_anchor_first is None:
            self._tempo_anchor_first = batch_first
            self._tempo_anchor_pulses = 0
        self._tempo_anchor_pulses += max(1, batch_count)
        span = batch_first - self._tempo_anchor_first
        if self._tempo_anchor_pulses < TEMPO_WINDOW_PULSES or span <= 0:
            return
        per_pulse = span / self._tempo_anchor_pulses
        if MIN_PERIOD_S <= per_pulse <= MAX_PERIOD_S:
            self._intervals.append(per_pulse)
            if len(self._intervals) > BPM_WINDOW:
                self._intervals.pop(0)
        self._tempo_anchor_first = batch_first
        self._tempo_anchor_pulses = 0

    def _on_pulse(self, now: float) -> None:
        """Count the pulse, re-anchor the phase, and sample the interval.

        Anchoring on the *arrival* rather than incrementing per message is what makes the
        phase survive both a dropout and a burst: after a stall the elapsed time already
        moved the phase, so the first pulse snaps it to the matching grid position and the
        pulses that follow in the same drain contribute no further steps.
        """
        self._pulses += 1
        if self._started is None:
            self._started = now
        if self._anchor_time is None:
            self._anchor_time = now
            self._anchor_index = 1.0
            self._intervals.clear()
            return
        median = self._median()
        interval = now - self._anchor_time
        if median and interval > 0:
            steps = max(0, int(round(interval / median)))
            if steps >= GAP_STEPS:
                self._gaps += 1
            self._anchor_index += steps
        elif not median:
            self._anchor_index += 1.0
        # Only plausible intervals feed the tempo estimate: a stall or a burst would drag it.
        # The **first** sample must pass the absolute tempo band too, not just the band around a
        # running median: the reader is often first polled after a long wait (the transport-start
        # settlement), and that first gap then became the median -- 251 pulses, a "0.0 BPM"
        # reading, and a phase locked to a 0.5 s "pulse" (`captures/...-121454` class).
        plausible = MIN_PERIOD_S <= interval <= MAX_PERIOD_S
        if plausible and (not median
                          or INTERVAL_BAND[0] * median <= interval <= INTERVAL_BAND[1] * median):
            self._intervals.append(interval)
            if len(self._intervals) > BPM_WINDOW:
                self._intervals.pop(0)
        self._anchor_time = now

    # ---- read-outs ---------------------------------------------------------
    def _median(self) -> float:
        return statistics.median(self._intervals) if self._intervals else 0.0

    def period(self) -> Optional[float]:
        """Seconds per clock pulse, or None until at least one interval is known."""
        return self._median() or None

    def bpm(self) -> Optional[float]:
        """Device tempo derived from its own clock.

        None outside the band the firmware itself accepts (``20..300`` BPM in
        ``ClockManager::onMidiClockPulse``): a transient during a clock-source transition can
        otherwise report a nonsense tempo, and a caller must not pace a phase on that.
        """
        period = self.period()
        if period is None:
            return None
        bpm = 60.0 / (period * MIDI_CLOCKS_PER_BEAT)
        return bpm if MIN_BPM <= bpm <= MAX_BPM else None

    def index_now(self) -> float:
        """Phase-locked pulse index: integer grid position, extrapolated between pulses."""
        if self._anchor_time is None:
            return 0.0
        median = self._median()
        if not median:
            return self._anchor_index
        elapsed = max(0.0, self._clock() - self._anchor_time)
        return self._anchor_index + elapsed / median

    def pulse_seen(self) -> bool:
        """True once a pulse has ever arrived (sticky)."""
        return self._started is not None

    def last_pulse_age_s(self) -> Optional[float]:
        """Seconds since the last pulse, or None if none ever arrived.

        ``pulse_seen`` is sticky, so it cannot answer "is the clock flowing *now*" -- and the
        device stops emitting clock when its transport stops. Callers that use the clock as
        evidence about the transport must use this instead.
        """
        if self._anchor_time is None:
            return None
        return max(0.0, self._clock() - self._anchor_time)

    def recent_pulse(self, window_s: Optional[float] = None) -> bool:
        """True when a pulse arrived within ``window_s`` (default: 4 pulse periods)."""
        age = self.last_pulse_age_s()
        if age is None:
            return False
        if window_s is None:
            window_s = max(0.25, 4.0 * (self.period() or 0.0))
        return age <= window_s

    def observed_pulses(self) -> int:
        """Raw count of received pulses, for evidence rather than for pacing."""
        return self._pulses

    def gap_count(self) -> int:
        return self._gaps

    def transport_messages(self) -> list[str]:
        """Realtime transport messages the device sent (``start``/``stop``/``continue``)."""
        return list(self._transport)

    def stats(self) -> dict[str, float]:
        return {
            "pulses": float(self._pulses),
            "phase_index": self.index_now(),
            "gaps": float(self._gaps),
            "period_ms": self._median() * 1e3,
            "bpm": self.bpm() or 0.0,
            "jitter_ms": statistics.pstdev(self._intervals) * 1e3 if len(self._intervals) > 1 else 0.0,
            "transport_messages": float(len(self._transport)),
        }

    # ---- synchronisation ---------------------------------------------------
    def wait_pulse(self, target: float, *, timeout_s: float, abort: Any = None) -> bool:
        """Block until phase ``target`` is reached. False on timeout or abort."""
        deadline = self._clock() + max(timeout_s, 0.0)
        while True:
            self.poll()
            if self.index_now() >= target:
                return True
            if abort is not None and abort.check() is not None:
                return False
            if self._clock() >= deadline:
                return False
            time.sleep(0.0002)

    def wait_bars(self, bars: int, *, timeout_s: float, abort: Any = None) -> bool:
        """Block until ``bars`` further bars of device clock have elapsed."""
        return self.wait_pulse(
            self.index_now() + bars * MIDI_CLOCKS_PER_BAR, timeout_s=timeout_s, abort=abort
        )


class MidiClockMaster:
    """Host-authoritative 24 PPQN clock with absolute-deadline pacing."""

    def __init__(self, out_port: mido.ports.BaseOutput, *, spin_s: float = 2.0e-4) -> None:
        self._out = out_port
        self._spin = spin_s
        self._sent = 0

    def start(self) -> None:
        self._out.send(mido.Message("start"))

    def stop(self) -> None:
        self._out.send(mido.Message("stop"))

    def send_pulses(self, count: int, *, bpm: float, abort: Any = None,
                    echo_reader: Optional[MidiClockReader] = None,
                    deadline_origin: Optional[float] = None,
                    deadline_index: int = 0) -> dict[str, float]:
        """Send ``count`` clock pulses at ``bpm``; return pacing quality for the report.

        Absolute deadlines (``origin + (i + 1) * period``) rather than per-step sleeps: a
        per-step sleep adds the send and loop overhead on top of every interval, and the
        device derives its tempo from pulse arrival times.

        ``deadline_origin``/``deadline_index`` let a caller that sends **one pulse at a time**
        keep one absolute schedule across those calls: pulse ``deadline_index + i`` is due at
        ``deadline_origin + (deadline_index + i) * period``. Without them the origin is this
        call's own ``perf_counter()``, so every call pays its own sleep/send overhead again
        (measured on this host: 3.7 ms per pulse, i.e. 101.8 BPM for a 120 BPM request, against
        120.0 BPM when the whole phase is one call).

        ``echo_reader`` is how the *device's* reaction is measured while the drive is still
        running. It must be a reader over the same input port. A device that is slaving stops
        emitting its own clock, but only once it has decided the external clock is really
        there (``ClockManager::checkClockSource`` runs in the main loop), so the report
        separates the pulses seen in the first quarter of the drive from the rest: only
        pulses in the tail mean the device is *still* clocking itself.
        """
        period = 60.0 / bpm / MIDI_CLOCKS_PER_BEAT
        call_origin = time.perf_counter()
        origin = call_origin if deadline_origin is None else deadline_origin
        errors: list[float] = []
        settle_after = max(1, int(count * 0.25))
        echo_total = 0
        echo_tail = 0
        for index in range(max(count, 0)):
            next_at = origin + (deadline_index + index + 1) * period
            while True:
                remaining = next_at - time.perf_counter()
                if remaining > self._spin:
                    time.sleep(remaining - self._spin)
                elif remaining > 0:
                    pass  # spin the last stretch
                else:
                    break
            if abort is not None and abort.check() is not None:
                break
            self._out.send(mido.Message("clock"))
            self._sent += 1
            errors.append(time.perf_counter() - next_at)
            if echo_reader is not None:
                seen = echo_reader.poll()
                echo_total += seen
                if index + 1 > settle_after:
                    echo_tail += seen
        worst = max((abs(e) for e in errors), default=0.0)
        return {
            "pulses_sent": float(self._sent),
            "max_err_ms": worst * 1e3,
            "mean_err_ms": (sum(abs(e) for e in errors) / len(errors) * 1e3) if errors else 0.0,
            "drift_ms": (time.perf_counter() - call_origin - len(errors) * period) * 1e3,
            "device_clock_echo_pulses": float(echo_total),
            "device_clock_echo_tail_pulses": float(echo_tail),
        }

    def bars(self, bars: int, *, bpm: float, abort: Any = None,
             echo_reader: Optional[MidiClockReader] = None) -> dict[str, float]:
        """Send ``bars`` bars of clock at ``bpm`` and return the pacing report."""
        return self.send_pulses(bars * MIDI_CLOCKS_PER_BAR, bpm=bpm, abort=abort,
                                echo_reader=echo_reader)
