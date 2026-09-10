"""Tail an external capture_session.py log without opening USB serial."""

from __future__ import annotations

import threading
import time
from pathlib import Path
from typing import Optional

from hitl.contract import CaptureIntegrity
from hitl.serial_collector import (
    CaptureLineClock,
    ChildPoll,
    _wait_for_serial_line_idle,
    _wait_for_serial_predicate,
    child_exit_abort_reason,
)


class ExternalSerialFollowCollector:
    """Polls a growing capture log written by capture_session.py.

    On start, bootstraps the tail of an existing log and seeks to EOF so HITL
    does not replay megabytes of history while the test is already running.
    """

    def __init__(
        self,
        log_path: Path,
        *,
        poll_interval_s: float = 0.05,
        out_dir: Path | None = None,
        bootstrap_tail_bytes: int = 256_000,
        bootstrap_tail_lines: int = 2000,
        child_poll: Optional[ChildPoll] = None,
    ) -> None:
        self._path = log_path
        self._poll_interval_s = max(poll_interval_s, 0.01)
        self._out_dir = out_dir
        self._bootstrap_tail_bytes = max(bootstrap_tail_bytes, 0)
        self._bootstrap_tail_lines = max(bootstrap_tail_lines, 1)
        self._lines: list[str] = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._error = ""
        self._clock = CaptureLineClock()
        self._started_at = time.monotonic()
        self._offset = 0
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._child_poll = child_poll
        self._path_resolved = log_path.resolve()
        self._watch_session_pointer = False

    @staticmethod
    def resolve_current_session_path(out_dir: Path | None = None) -> Path:
        base = out_dir or Path("captures")
        pointer = base / ".current_session"
        if not pointer.is_file():
            raise FileNotFoundError(
                f"Missing {pointer} — start capture_session.py in another terminal first."
            )
        raw = pointer.read_text(encoding="utf-8").strip()
        if not raw:
            raise FileNotFoundError(f"{pointer} is empty")
        path = Path(raw)
        if not path.is_absolute():
            path = (Path.cwd() / path).resolve()
        return path

    def _bootstrap_from_file(self) -> None:
        if not self._path.is_file():
            return
        try:
            with self._path.open("rb") as log_file:
                size = log_file.seek(0, 2)
                if size <= 0:
                    self._offset = 0
                    return
                start = max(0, size - self._bootstrap_tail_bytes)
                log_file.seek(start)
                if start > 0:
                    log_file.readline()
                tail_lines: list[str] = []
                for raw in log_file:
                    line = raw.decode("utf-8", errors="replace").strip()
                    if line:
                        tail_lines.append(line)
                if len(tail_lines) > self._bootstrap_tail_lines:
                    tail_lines = tail_lines[-self._bootstrap_tail_lines :]
                with self._lock:
                    self._lines = tail_lines
                    if tail_lines:
                        now = time.monotonic()
                        for line in tail_lines:
                            self._clock.note_line(line, now=now)
                    self._clock.baseline_index = len(self._lines)
                self._offset = size
        except OSError as exc:
            self._error = str(exc)
            with self._lock:
                self._clock.mark_incomplete(f"serial follow error: {exc}")

    def start(self) -> None:
        self._started_at = time.monotonic()
        if self._child_poll is None:
            from hitl.managed_capture import active_managed_capture

            session = active_managed_capture()
            if session is not None:
                self._child_poll = session.poll
        self._bootstrap_from_file()
        self._watch_session_pointer = self._pointer_matches_followed_file()
        self._thread.start()

    def set_child_poll(self, poll: Optional[ChildPoll]) -> None:
        self._child_poll = poll

    def _mark_incomplete(self, reason: str) -> None:
        with self._lock:
            self._clock.mark_incomplete(reason)

    def _current_session_pointer_path(self) -> Optional[Path]:
        if self._out_dir is None:
            return None
        pointer = self._out_dir / ".current_session"
        if not pointer.is_file():
            return None
        raw = pointer.read_text(encoding="utf-8").strip()
        if not raw:
            return None
        current = Path(raw)
        if not current.is_absolute():
            current = (Path.cwd() / current).resolve()
        else:
            current = current.resolve()
        return current

    def _pointer_matches_followed_file(self) -> bool:
        current = self._current_session_pointer_path()
        return current is not None and current == self._path_resolved

    def _file_integrity_reason(self) -> Optional[str]:
        if not self._path.is_file():
            return f"capture file missing: {self._path}"
        size = self._path.stat().st_size
        if size < self._offset:
            return f"capture file truncated ({size} < offset {self._offset})"
        if self._watch_session_pointer:
            pointer = self._out_dir / ".current_session" if self._out_dir is not None else None
            if pointer is not None and pointer.is_file() and not pointer.read_text(encoding="utf-8").strip():
                return None
            current = self._current_session_pointer_path()
            if current is None:
                return f"capture session pointer missing under {self._out_dir}"
            if current != self._path_resolved:
                return f"capture file rotated to {current}"
        return None

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2.0)

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                integrity_reason = self._file_integrity_reason()
                if integrity_reason:
                    self._mark_incomplete(integrity_reason)
                    time.sleep(self._poll_interval_s)
                    continue
                if not self._path.is_file():
                    time.sleep(self._poll_interval_s)
                    continue
                with self._path.open("rb") as log_file:
                    log_file.seek(self._offset)
                    while not self._stop.is_set():
                        raw = log_file.readline()
                        if not raw:
                            break
                        self._offset = log_file.tell()
                        line = raw.decode("utf-8", errors="replace").strip()
                        if not line:
                            continue
                        with self._lock:
                            self._lines.append(line)
                            self._clock.note_line(line)
            except OSError as exc:
                self._error = str(exc)
                self._mark_incomplete(f"serial follow error: {exc}")
            time.sleep(self._poll_interval_s)

    def snapshot(self) -> list[str]:
        with self._lock:
            return list(self._lines)

    def line_count(self) -> int:
        with self._lock:
            return len(self._lines)

    def drain_until_idle(
        self,
        *,
        idle_ms: int = 750,
        max_drain_ms: int = 10000,
    ) -> int:
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
        raise RuntimeError(
            "External serial follow cannot send host commands; use --serial-port for revision scenarios."
        )

    def capture_abort_reason(self, timeout_s: float) -> Optional[str]:
        child_reason = child_exit_abort_reason(self._child_poll)
        if child_reason:
            self._mark_incomplete(child_reason)
            return child_reason
        with self._lock:
            integrity_reason = self._clock.integrity_abort_reason()
        if integrity_reason:
            return integrity_reason
        live_integrity = self._file_integrity_reason()
        if live_integrity:
            self._mark_incomplete(live_integrity)
            return live_integrity
        if timeout_s <= 0:
            return None
        return self.heartbeat_abort_reason(timeout_s)

    def heartbeat_abort_reason(self, timeout_s: float) -> Optional[str]:
        if self._error:
            return f"serial follow error: {self._error}"
        now = time.monotonic()
        with self._lock:
            last_at = self._clock.last_firmware_at
            started_at = self._started_at
        if last_at is None:
            elapsed = now - started_at
            if elapsed > timeout_s:
                return (
                    f"serial follow: no meaningful lines from {self._path} within "
                    f"{elapsed:.1f}s (limit {timeout_s:.1f}s; is capture_session.py running?)"
                )
            return None
        elapsed = now - last_at
        if elapsed > timeout_s:
            return (
                f"serial heartbeat lost ({elapsed:.1f}s since last meaningful line, "
                f"limit {timeout_s:.1f}s)"
            )
        return None
