"""Tail an external capture_session.py log without opening USB serial."""

from __future__ import annotations

import threading
import time
from pathlib import Path
from typing import Optional


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
    ) -> None:
        self._path = log_path
        self._poll_interval_s = max(poll_interval_s, 0.01)
        self._out_dir = out_dir or Path("captures")
        self._bootstrap_tail_bytes = max(bootstrap_tail_bytes, 0)
        self._bootstrap_tail_lines = max(bootstrap_tail_lines, 1)
        self._lines: list[str] = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._error = ""
        self._last_line_at: Optional[float] = None
        self._offset = 0
        self._thread = threading.Thread(target=self._run, daemon=True)

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
                        self._last_line_at = time.monotonic()
                self._offset = size
        except OSError as exc:
            self._error = str(exc)

    def start(self) -> None:
        self._bootstrap_from_file()
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2.0)

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
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
                            self._last_line_at = time.monotonic()
            except OSError as exc:
                self._error = str(exc)
            time.sleep(self._poll_interval_s)

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
        raise RuntimeError(
            "External serial follow cannot send host commands; use --serial-port for revision scenarios."
        )

    def heartbeat_abort_reason(self, timeout_s: float) -> Optional[str]:
        if self._error:
            return f"serial follow error: {self._error}"
        with self._lock:
            if self._last_line_at is None:
                if self._path.is_file() and self._path.stat().st_size > 0 and self._offset == 0:
                    return None
                return (
                    f"serial follow: no lines yet from {self._path} "
                    "(is capture_session.py running?)"
                )
            elapsed = time.monotonic() - self._last_line_at
        if elapsed > timeout_s:
            return (
                f"serial heartbeat lost ({elapsed:.1f}s since last line, "
                f"limit {timeout_s:.1f}s)"
            )
        return None
