"""USB serial capture collector and run-abort watchdog for HITL."""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from typing import Any, Optional

import serial
from serial import SerialException


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
        if self.serial_collector is not None and self.heartbeat_timeout_s > 0:
            return self.serial_collector.heartbeat_abort_reason(self.heartbeat_timeout_s)
        return None


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
        self._last_line_at: Optional[float] = None
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
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
                    self._last_line_at = time.monotonic()
        except SerialException as exc:
            self._error = str(exc)
            self._stop.set()

    def snapshot(self) -> list[str]:
        with self._lock:
            return list(self._lines)

    def error(self) -> str:
        return self._error

    def seconds_since_last_line(self) -> Optional[float]:
        with self._lock:
            if self._last_line_at is None:
                return None
            return time.monotonic() - self._last_line_at

    def write_line(self, text: str) -> None:
        payload = (text.rstrip("\n") + "\n").encode("utf-8")
        self._serial.write(payload)
        self._serial.flush()

    def heartbeat_abort_reason(self, timeout_s: float) -> Optional[str]:
        if self._error:
            return f"serial read error: {self._error}"
        with self._lock:
            if self._last_line_at is None:
                return None
            elapsed = time.monotonic() - self._last_line_at
        if elapsed > timeout_s:
            return (
                f"serial heartbeat lost ({elapsed:.1f}s since last line, "
                f"limit {timeout_s:.1f}s)"
            )
        return None
