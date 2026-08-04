"""Estimate Teensy transport tick from #CAP,BAR serial lines + BPM."""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Any, Optional

from hitl.control_constants import MIDI_CLOCKS_PER_BEAT, TICKS_PER_BEAT

# Globals.h: INTERNAL_PPQN / 24 = 8 ticks per MIDI clock pulse.
TICKS_PER_CLOCK = TICKS_PER_BEAT // MIDI_CLOCKS_PER_BEAT

# Firmware ButtonProcessor short-press debounce window (see BTN "window=300" logs).
TRANSPORT_SHORT_DEBOUNCE_MS = 300


def ticks_per_second_from_bpm(bpm: float) -> float:
    """Transport ticks per second at ``bpm`` (matches ClockManager rate)."""
    if bpm <= 0:
        return 0.0
    return (bpm / 60.0) * float(TICKS_PER_BEAT)


def transport_record_start_monotonic(press_sent_mono: float, *, press_ms: int) -> float:
    """Estimate wall time when transport short-press starts recording on the device."""
    return press_sent_mono + (max(press_ms, 0) + TRANSPORT_SHORT_DEBOUNCE_MS) / 1000.0


def record_button_lead_seconds(*, press_ms: int) -> float:
    """Lead time before a bar boundary to press record/transport (short-press debounce)."""
    return (max(press_ms, 0) + TRANSPORT_SHORT_DEBOUNCE_MS) / 1000.0


def parse_latest_cap_bar_tick(lines: list[str], *, after_index: int = 0) -> Optional[int]:
    """Return transport tick from the most recent #CAP,...,BAR line."""
    last: Optional[int] = None
    for line in lines[after_index:]:
        if "#CAP," not in line or ",BAR," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 5:
            continue
        try:
            last = int(parts[3])
        except ValueError:
            continue
    return last


def parse_latest_cap_reca_tick(lines: list[str], *, after_index: int = 0) -> Optional[int]:
    """Return loop-relative record start tick from the most recent #CAP,...,RECA line."""
    last: Optional[int] = None
    for line in lines[after_index:]:
        if "#CAP," not in line or ",RECA," not in line:
            continue
        parts = line.split(",")
        if len(parts) >= 5:
            try:
                last = int(parts[4])
            except ValueError:
                continue
    return last


def parse_human_recording_started_tick(lines: list[str], *, after_index: int = 0) -> Optional[int]:
    """Fallback when #CAP,RECA was dropped (ring overflow): human debug log line."""
    import re

    pattern = re.compile(r"Recording started @ tick (\d+)")
    last: Optional[int] = None
    for line in lines[after_index:]:
        match = pattern.search(line)
        if match:
            last = int(match.group(1))
    return last


def parse_record_start_tick(lines: list[str], *, after_index: int = 0) -> Optional[int]:
    """Record-start transport tick from #CAP RECA or human log fallback."""
    reca = parse_latest_cap_reca_tick(lines, after_index=after_index)
    if reca is not None:
        return reca
    return parse_human_recording_started_tick(lines, after_index=after_index)


@dataclass
class TransportTickAnchor:
    """Extrapolate transport tick from the latest BAR anchor and BPM."""

    anchor_tick: int
    anchor_mono: float
    ticks_per_second: float
    last_line_index: int = 0

    @classmethod
    def from_collector(
        cls,
        collector: Any,
        bpm: float,
        *,
        after_index: int = 0,
        fallback_tick: Optional[int] = None,
    ) -> TransportTickAnchor:
        lines = collector.snapshot()
        bar_tick = parse_latest_cap_bar_tick(lines, after_index=after_index)
        reca_tick = parse_latest_cap_reca_tick(lines, after_index=after_index)
        tick = bar_tick if bar_tick is not None else reca_tick
        if tick is None:
            tick = fallback_tick if fallback_tick is not None else 0
        return cls(
            anchor_tick=tick,
            anchor_mono=time.monotonic(),
            ticks_per_second=ticks_per_second_from_bpm(bpm),
            last_line_index=len(lines),
        )

    def refresh(self, collector: Any) -> None:
        lines = collector.snapshot()
        for index in range(self.last_line_index, len(lines)):
            line = lines[index]
            if "#CAP," not in line or ",BAR," not in line:
                continue
            parts = line.split(",")
            if len(parts) < 5:
                continue
            try:
                self.anchor_tick = int(parts[3])
                self.anchor_mono = time.monotonic()
            except ValueError:
                continue
        self.last_line_index = len(lines)

    def estimated_tick(self, now_mono: Optional[float] = None) -> int:
        now = time.monotonic() if now_mono is None else now_mono
        elapsed = max(0.0, now - self.anchor_mono)
        return int(self.anchor_tick + elapsed * self.ticks_per_second)

    def sleep_until_tick(
        self,
        target_tick: int,
        collector: Any,
        *,
        poll_s: float = 0.002,
        abort: Any = None,
    ) -> bool:
        """Block until ``estimated_tick`` reaches ``target_tick`` (refreshes BAR anchor)."""
        while self.estimated_tick() < target_tick:
            if abort is not None and abort.check() is not None:
                return False
            self.refresh(collector)
            remaining = target_tick - self.estimated_tick()
            if remaining <= 0:
                break
            sleep_s = remaining / self.ticks_per_second if self.ticks_per_second > 0 else poll_s
            time.sleep(min(max(sleep_s, poll_s), 0.05))
        return True
