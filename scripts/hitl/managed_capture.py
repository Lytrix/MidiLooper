"""Spawn capture_session.py alongside HITL (managed dual-session runs)."""

from __future__ import annotations

import signal
import subprocess
import sys
import threading
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Callable, Iterator, Optional

from hitl.config import DEFAULT_CAPTURE_SERIAL_PORT, HitlConfig

_CAPTURE_SCRIPT = Path(__file__).resolve().parent.parent / "capture_session.py"


def wait_for_current_session_pointer(
    out_dir: Path,
    *,
    timeout_s: float = 30.0,
    poll_s: float = 0.1,
    started_after: float | None = None,
) -> Path:
    pointer = out_dir / ".current_session"
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        if pointer.is_file():
            raw = pointer.read_text(encoding="utf-8").strip()
            if raw:
                path = Path(raw)
                if not path.is_absolute():
                    path = (Path.cwd() / path).resolve()
                if started_after is not None:
                    if not path.is_file():
                        time.sleep(poll_s)
                        continue
                    if path.stat().st_mtime < started_after - 0.25:
                        time.sleep(poll_s)
                        continue
                return path
        time.sleep(poll_s)
    raise TimeoutError(
        f"Timed out waiting for {pointer} — capture_session did not publish a session log"
    )


class ManagedCaptureSession:
    """Background capture_session.py process for HITL device runs."""

    def __init__(
        self,
        *,
        port: str = DEFAULT_CAPTURE_SERIAL_PORT,
        boot_wait_s: float = 0.0,
        out_dir: Path,
        python_executable: Optional[str] = None,
    ) -> None:
        self._port = port
        self._boot_wait_s = max(boot_wait_s, 0.0)
        self._out_dir = out_dir
        self._python = python_executable or sys.executable
        self._process: subprocess.Popen[str] | None = None
        self._output_thread: threading.Thread | None = None
        self._session_log: Path | None = None

    @property
    def session_log(self) -> Path | None:
        return self._session_log

    def start(self) -> Path:
        if not _CAPTURE_SCRIPT.is_file():
            raise FileNotFoundError(f"Missing capture script: {_CAPTURE_SCRIPT}")
        self._out_dir.mkdir(parents=True, exist_ok=True)
        pointer = self._out_dir / ".current_session"
        if pointer.is_file():
            pointer.unlink()
        command = [
            self._python,
            str(_CAPTURE_SCRIPT),
            "--port",
            self._port,
            "--boot-wait",
            str(self._boot_wait_s),
            "--out-dir",
            str(self._out_dir),
        ]
        if self._boot_wait_s > 0:
            print(
                f"[hitl-capture] starting capture_session.py on {self._port} "
                f"(boot-wait {self._boot_wait_s:.1f}s)",
                flush=True,
            )
        else:
            print(
                f"[hitl-capture] starting capture_session.py on {self._port}",
                flush=True,
            )
        started_at = time.monotonic()
        self._process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        self._output_thread = threading.Thread(target=self._drain_output, daemon=True)
        self._output_thread.start()
        self._session_log = wait_for_current_session_pointer(
            self._out_dir,
            started_after=started_at,
        )
        print(f"[hitl-capture] session log: {self._session_log}", flush=True)
        return self._session_log

    def stop(self) -> None:
        process = self._process
        if process is None:
            return
        if process.poll() is None:
            print("[hitl-capture] stopping capture_session.py", flush=True)
            process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=8.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2.0)
        self._process = None

    def _drain_output(self) -> None:
        process = self._process
        if process is None or process.stdout is None:
            return
        for line in process.stdout:
            print(f"[capture] {line.rstrip()}", flush=True)

    def __enter__(self) -> ManagedCaptureSession:
        self.start()
        return self

    def __exit__(self, *_exc: object) -> None:
        self.stop()


@contextmanager
def managed_capture_session(config: HitlConfig) -> Iterator[Path]:
    session = ManagedCaptureSession(
        port=config.capture_serial_port,
        boot_wait_s=config.capture_boot_wait_s,
        out_dir=config.out_dir,
    )
    try:
        log_path = session.start()
        yield log_path
    finally:
        session.stop()


def run_with_managed_capture(config: HitlConfig, run_fn: Callable[[HitlConfig], int]) -> int:
    active = config.with_follow_current_session()
    with managed_capture_session(config):
        return run_fn(active)


def legacy_args_with_managed_capture(legacy: list[str], args: object) -> list[str]:
    if getattr(args, "verify_only", False):
        return legacy
    if getattr(args, "no_managed_capture", False):
        return legacy
    if "--serial-port" in legacy:
        return legacy
    merged = list(legacy)
    if "--follow-current-session" not in merged:
        merged.append("--follow-current-session")
    return merged
