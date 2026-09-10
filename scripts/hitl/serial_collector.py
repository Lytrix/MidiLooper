"""USB serial capture collector and run-abort watchdog for HITL."""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from typing import Any, Callable, Optional

import serial
from serial import SerialException

from hitl.contract import CaptureIntegrity

_TRANSPORT_MARKERS = (
    ",BPM,",
    ",BAR,",
    ",DFRAME,",
    ",MI,",
    ",MO,",
    ",ST,",
    ",RECA,",
    ",RECS,",
)

ChildPoll = Callable[[], Optional[int]]


def serial_line_resets_heartbeat(line: str) -> bool:
    """True for firmware-progress lines. Keepalives must not mask HITL stalls."""
    if ",DFRAME," in line:
        return False
    if ",PERS,work," in line:
        return False
    if ",PERS,mid_pass," in line:
        return False
    return True


def serial_line_is_transport(line: str) -> bool:
    """True when the line shows sequencer/transport activity."""
    if "#CAP," not in line:
        return False
    return any(marker in line for marker in _TRANSPORT_MARKERS)


@dataclass
class CaptureLineClock:
    """Separate reader, firmware, and transport liveness plus integrity."""

    baseline_index: int = 0
    last_reader_at: Optional[float] = None
    last_firmware_at: Optional[float] = None
    last_transport_at: Optional[float] = None
    integrity: CaptureIntegrity = CaptureIntegrity.UNKNOWN
    integrity_reason: str = ""

    def note_line(self, line: str, *, now: Optional[float] = None) -> None:
        stamp = now if now is not None else time.monotonic()
        self.last_reader_at = stamp
        if serial_line_resets_heartbeat(line):
            self.last_firmware_at = stamp
        if serial_line_is_transport(line):
            self.last_transport_at = stamp
        if self.integrity is CaptureIntegrity.UNKNOWN:
            self.integrity = CaptureIntegrity.COMPLETE

    def mark_incomplete(self, reason: str) -> None:
        self.integrity = CaptureIntegrity.INCOMPLETE
        self.integrity_reason = reason

    def seconds_since(self, stamp: Optional[float], *, now: Optional[float] = None) -> Optional[float]:
        if stamp is None:
            return None
        return (now if now is not None else time.monotonic()) - stamp

    def seconds_since_last_reader_line(self, *, now: Optional[float] = None) -> Optional[float]:
        return self.seconds_since(self.last_reader_at, now=now)

    def seconds_since_last_firmware_line(self, *, now: Optional[float] = None) -> Optional[float]:
        return self.seconds_since(self.last_firmware_at, now=now)

    def seconds_since_last_transport_line(self, *, now: Optional[float] = None) -> Optional[float]:
        return self.seconds_since(self.last_transport_at, now=now)

    def integrity_abort_reason(self) -> Optional[str]:
        if self.integrity is CaptureIntegrity.INCOMPLETE and self.integrity_reason:
            return self.integrity_reason
        return None


def child_exit_abort_reason(poll: Optional[ChildPoll]) -> Optional[str]:
    if poll is None:
        return None
    code = poll()
    if code is None:
        return None
    return f"managed capture child exited ({code})"


@dataclass
class RunAbort:
    """Optional hard deadline plus serial heartbeat watchdog."""

    run_deadline: Optional[float] = None
    serial_collector: Optional[Any] = None
    heartbeat_timeout_s: float = 20.0

    def check(self) -> Optional[str]:
        now = time.monotonic()
        if self.run_deadline is not None and now >= self.run_deadline:
            return "run deadline exceeded"
        collector = self.serial_collector
        if collector is None:
            return None
        capture_abort = getattr(collector, "capture_abort_reason", None)
        if callable(capture_abort):
            return capture_abort(self.heartbeat_timeout_s)
        if self.heartbeat_timeout_s > 0:
            return collector.heartbeat_abort_reason(self.heartbeat_timeout_s)
        return None


def _wait_for_serial_predicate(
    snapshot_fn: Any,
    predicate: Any,
    *,
    max_drain_ms: int,
    poll_s: float = 0.05,
) -> bool:
    """Poll snapshot_fn until predicate(lines) is true or max_drain_ms elapses."""
    if max_drain_ms <= 0:
        return bool(predicate(snapshot_fn()))
    deadline = time.monotonic() + max(max_drain_ms, 0) / 1000.0
    while time.monotonic() < deadline:
        if predicate(snapshot_fn()):
            return True
        time.sleep(max(poll_s, 0.001))
    return bool(predicate(snapshot_fn()))


def _wait_for_serial_line_idle(
    line_count_fn: Any,
    *,
    idle_ms: int,
    max_drain_ms: int,
    poll_s: float = 0.02,
) -> int:
    """Wait until serial line count is stable for idle_ms (USB TX / deferred #CAP flush)."""
    if max_drain_ms <= 0:
        return 0
    start_len = int(line_count_fn())
    idle_s = max(idle_ms, 0) / 1000.0
    deadline = time.monotonic() + max(max_drain_ms, 0) / 1000.0
    last_len = start_len
    last_change_at = time.monotonic()
    while time.monotonic() < deadline:
        current_len = int(line_count_fn())
        if current_len > last_len:
            last_len = current_len
            last_change_at = time.monotonic()
        elif idle_s <= 0.0 or (time.monotonic() - last_change_at) >= idle_s:
            break
        time.sleep(max(poll_s, 0.001))
    return last_len - start_len


class SerialCaptureCollector:
    """Collects Teensy serial lines in a background thread."""

    def __init__(self, port: str, baud: int, timeout: float = 0.05) -> None:
        self._serial = serial.Serial(port, baud, timeout=timeout)
        self._lines: list[str] = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._error: str = ""
        self._clock = CaptureLineClock()
        self._started_at = time.monotonic()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._child_poll: Optional[ChildPoll] = None

    def start(self) -> None:
        self._started_at = time.monotonic()
        self._thread.start()

    def line_count(self) -> int:
        with self._lock:
            return len(self._lines)

    def drain_until_idle(
        self,
        *,
        idle_ms: int = 750,
        max_drain_ms: int = 10000,
    ) -> int:
        """Keep the reader thread running until trailing serial lines stop arriving."""
        return _wait_for_serial_line_idle(
            self.line_count,
            idle_ms=idle_ms,
            max_drain_ms=max_drain_ms,
        )

    def drain_until_dnte_src_dumps(
        self,
        *,
        max_drain_ms: int,
        after_ts: Optional[int] = None,
        max_expected: Optional[int] = None,
        required_kinds: Optional[tuple[str, ...]] = None,
    ) -> bool:
        """Wait until the required DNTE source dumps are complete."""
        from hitl.verify.dnte_src_dumps import dnte_src_dump_trio_complete

        kinds = required_kinds if required_kinds is not None else ("effective", "frame")

        def _predicate(lines: list[str]) -> bool:
            return dnte_src_dump_trio_complete(
                lines,
                after_ts=after_ts,
                max_expected=max_expected,
                required_kinds=kinds,
            )

        return _wait_for_serial_predicate(
            self.snapshot,
            _predicate,
            max_drain_ms=max_drain_ms,
        )

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2.0)
        self._serial.close()

    def _run(self) -> None:
        try:
            while not self._stop.is_set():
                raw = self._serial.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                with self._lock:
                    self._lines.append(line)
                    self._clock.note_line(line)
        except SerialException as exc:
            self._error = str(exc)
            with self._lock:
                self._clock.mark_incomplete(f"serial read error: {exc}")
            self._stop.set()

    def snapshot(self) -> list[str]:
        with self._lock:
            return list(self._lines)

    def error(self) -> str:
        return self._error

    def capture_integrity(self) -> CaptureIntegrity:
        with self._lock:
            return self._clock.integrity

    def session_start_index(self) -> int:
        with self._lock:
            return self._clock.baseline_index

    def capture_integrity_reason(self) -> str:
        with self._lock:
            return self._clock.integrity_reason

    def seconds_since_last_line(self) -> Optional[float]:
        """Firmware-progress age. DFRAME keepalives do not reset this."""
        with self._lock:
            return self._clock.seconds_since_last_firmware_line()

    def seconds_since_last_reader_line(self) -> Optional[float]:
        with self._lock:
            return self._clock.seconds_since_last_reader_line()

    def seconds_since_last_transport_line(self) -> Optional[float]:
        with self._lock:
            return self._clock.seconds_since_last_transport_line()

    def write_line(self, text: str) -> None:
        payload = (text.rstrip("\n") + "\n").encode("utf-8")
        self._serial.write(payload)
        self._serial.flush()

    def capture_abort_reason(self, timeout_s: float) -> Optional[str]:
        child_reason = child_exit_abort_reason(self._child_poll)
        if child_reason:
            with self._lock:
                self._clock.mark_incomplete(child_reason)
            return child_reason
        with self._lock:
            integrity_reason = self._clock.integrity_abort_reason()
        if integrity_reason:
            return integrity_reason
        if timeout_s <= 0:
            return None
        return self.heartbeat_abort_reason(timeout_s)

    def heartbeat_abort_reason(self, timeout_s: float) -> Optional[str]:
        if self._error:
            return f"serial read error: {self._error}"
        now = time.monotonic()
        with self._lock:
            last_at = self._clock.last_firmware_at
            started_at = self._started_at
        if last_at is None:
            elapsed = now - started_at
            if elapsed > timeout_s:
                return (
                    f"serial capture: no meaningful lines within {elapsed:.1f}s "
                    f"(limit {timeout_s:.1f}s)"
                )
            return None
        elapsed = now - last_at
        if elapsed > timeout_s:
            return (
                f"serial heartbeat lost ({elapsed:.1f}s since last meaningful line, "
                f"limit {timeout_s:.1f}s)"
            )
        return None
