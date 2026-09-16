"""Spawn capture_session.py alongside HITL (managed dual-session runs)."""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import threading
import time
from contextlib import contextmanager
from dataclasses import replace
from pathlib import Path
from typing import Callable, Iterator, Optional

from hitl.config import DEFAULT_CAPTURE_SERIAL_PORT, HitlConfig

_CAPTURE_SCRIPT = Path(__file__).resolve().parent.parent / "capture_session.py"
_ACTIVE_LOCK = threading.Lock()
_ACTIVE_SESSION: Optional["ManagedCaptureSession"] = None

# Hard ceiling on one managed HITL run. The scenario has its own
# `--max-run-seconds` deadline, but a stalled phase or a serial-follow wait must
# never keep the harness (and the USB serial port) occupied forever. Zero or
# negative disables the ceiling; `HITL_MANAGED_RUN_TIMEOUT_S` overrides the
# default. Six minutes covers the dense record/overdub recipes; raise it for
# unusually long layered runs.
DEFAULT_MANAGED_RUN_TIMEOUT_S = 360.0
_RUN_TIMEOUT_ENV = "HITL_MANAGED_RUN_TIMEOUT_S"


def resolve_managed_run_timeout(timeout_s: Optional[float] = None) -> float:
    """Resolve the managed-run watchdog ceiling in seconds (<= 0 disables)."""
    if timeout_s is not None:
        return max(float(timeout_s), 0.0)
    raw = os.environ.get(_RUN_TIMEOUT_ENV)
    if raw:
        try:
            return max(float(raw), 0.0)
        except ValueError:
            pass
    return DEFAULT_MANAGED_RUN_TIMEOUT_S


def active_managed_capture() -> Optional["ManagedCaptureSession"]:
    with _ACTIVE_LOCK:
        return _ACTIVE_SESSION


def _set_active_managed_capture(session: Optional["ManagedCaptureSession"]) -> None:
    global _ACTIVE_SESSION
    with _ACTIVE_LOCK:
        _ACTIVE_SESSION = session


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


def wait_for_capture_boot_ready(
    out_dir: Path,
    session_log: Path,
    *,
    timeout_s: float,
    poll_s: float = 0.1,
) -> None:
    """Wait for capture_session's first serial-open boot settle.

    The managed runner must not send control MIDI while opening serial has reset
    the Teensy.  The ready marker binds this wait to the exact session log so a
    stale marker from an earlier run cannot authorize the new scenario.
    """
    ready = out_dir / ".current_session.ready"
    deadline = time.monotonic() + max(timeout_s, 0.0)
    expected = session_log.resolve()
    while time.monotonic() < deadline:
        if ready.is_file():
            raw = ready.read_text(encoding="utf-8").strip()
            if raw:
                candidate = Path(raw)
                if not candidate.is_absolute():
                    candidate = (Path.cwd() / candidate).resolve()
                if candidate == expected:
                    return
        time.sleep(poll_s)
    raise TimeoutError(
        f"Timed out waiting for {ready} — capture_session did not finish boot settle"
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
        arm_snapshot_yield: bool = False,
        run_token: str | None = None,
    ) -> None:
        self._port = port
        self._boot_wait_s = max(boot_wait_s, 0.0)
        self._out_dir = out_dir
        self._python = python_executable or sys.executable
        self._arm_snapshot_yield = arm_snapshot_yield
        self._run_token = run_token
        self._process: subprocess.Popen[str] | None = None
        self._output_thread: threading.Thread | None = None
        self._session_log: Path | None = None
        self._owner_write_fd: int | None = None

    @property
    def session_log(self) -> Path | None:
        return self._session_log

    def start(self) -> Path:
        if not _CAPTURE_SCRIPT.is_file():
            raise FileNotFoundError(f"Missing capture script: {_CAPTURE_SCRIPT}")
        self._out_dir.mkdir(parents=True, exist_ok=True)
        pointer = self._out_dir / ".current_session"
        ready_pointer = self._out_dir / ".current_session.ready"
        if pointer.is_file():
            pointer.unlink()
        if ready_pointer.is_file():
            ready_pointer.unlink()
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
        if self._run_token is not None:
            command += ["--run-token", self._run_token]
        if self._arm_snapshot_yield:
            command.append("--arm-snapshot-yield")
        # Parent-death pipe: the child exits when this process closes the write
        # end, which happens even if the harness is SIGKILLed. That guarantees a
        # leaked capture session can never keep the USB serial port open.
        owner_read_fd, owner_write_fd = os.pipe()
        self._owner_write_fd = owner_write_fd
        command += ["--owner-fd", str(owner_read_fd)]
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
        try:
            self._process = subprocess.Popen(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                pass_fds=(owner_read_fd,),
            )
        finally:
            os.close(owner_read_fd)
        self._output_thread = threading.Thread(target=self._drain_output, daemon=True)
        self._output_thread.start()
        self._session_log = wait_for_current_session_pointer(
            self._out_dir,
            started_after=started_at,
        )
        print(f"[hitl-capture] session log: {self._session_log}", flush=True)
        wait_for_capture_boot_ready(
            self._out_dir,
            self._session_log,
            timeout_s=max(self._boot_wait_s + 30.0, 30.0),
        )
        print("[hitl-capture] boot settle complete", flush=True)
        _set_active_managed_capture(self)
        return self._session_log

    def poll(self) -> Optional[int]:
        process = self._process
        if process is None:
            return None
        return process.poll()

    def child_exit_reason(self) -> Optional[str]:
        code = self.poll()
        if code is None:
            return None
        return f"managed capture child exited ({code})"

    def stop(self) -> None:
        try:
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
                    try:
                        process.wait(timeout=2.0)
                    except subprocess.TimeoutExpired:
                        print(
                            "[hitl-capture] capture_session.py did not exit after SIGKILL",
                            flush=True,
                        )
            if process.stdout is not None:
                process.stdout.close()
            self._process = None
        finally:
            if self._owner_write_fd is not None:
                try:
                    os.close(self._owner_write_fd)
                except OSError:
                    pass
                self._owner_write_fd = None
            if active_managed_capture() is self:
                _set_active_managed_capture(None)

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
        arm_snapshot_yield=config.arm_snapshot_yield,
    )
    try:
        log_path = session.start()
        yield log_path
    finally:
        session.stop()


def _run_with_watchdog(run_fn: Callable[[], int], timeout_s: float) -> int:
    """Run `run_fn` on a worker thread behind a wall-clock ceiling.

    Returns the run code, or 3 if the ceiling is exceeded. Exceptions from the
    worker are re-raised here so callers keep normal failure semantics.
    """
    if timeout_s <= 0:
        return run_fn()
    outcome: dict[str, object] = {}

    def _worker() -> None:
        try:
            outcome["code"] = run_fn()
        except BaseException as exc:  # re-raised on the owner thread
            outcome["exc"] = exc

    worker = threading.Thread(target=_worker, name="hitl-managed-run", daemon=True)
    worker.start()
    worker.join(timeout_s)
    if worker.is_alive():
        print(
            f"[hitl-capture] scenario exceeded {timeout_s:.1f}s watchdog; "
            "aborting run and releasing capture",
            flush=True,
        )
        return 3
    if "exc" in outcome:
        raise outcome["exc"]  # type: ignore[misc]
    return int(outcome.get("code", 3))


def run_with_managed_capture(
    config: HitlConfig,
    run_fn: Callable[[HitlConfig], int],
    *,
    timeout_s: Optional[float] = None,
) -> int:
    """Run the scenario with the managed capture child.

    The scenario runs behind a wall-clock watchdog. If it stalls, the watchdog
    returns and the `managed_capture_session` context stops the capture child, so
    the harness always ends itself and releases the USB serial port.
    `timeout_s <= 0` (or `HITL_MANAGED_RUN_TIMEOUT_S=0`) disables the watchdog
    for intentionally long runs.
    """
    timeout = resolve_managed_run_timeout(timeout_s)
    with managed_capture_session(config) as log_path:
        active = replace(config, follow_current_session=False, follow_serial_log=log_path,
                         legacy_extra_args=tuple(legacy_args_with_managed_capture(
                             list(config.legacy_extra_args), config, log_path)))
        write_metadata_best_effort(active, session_log=log_path)
        return _run_with_watchdog(lambda: run_fn(active), timeout)


def write_metadata_best_effort(config: HitlConfig, *, session_log: Optional[Path] = None) -> None:
    """Record this run's identity next to its report (HITL_ARCHITECTURE.md §11.1).

    Best-effort: a missing git binary or firmware hex records null and never fails
    the run.
    """
    from hitl.run_metadata import write_run_metadata

    try:
        path = write_run_metadata(
            config.out_dir,
            preset=config.preset,
            scenario_ids=config.scenario_ids,
            track_number=config.track_number,
            loop_slot=config.loop_slot,
            midi_channel=config.midi_channel,
            record_bars=config.record_bars,
            overdub_bars=config.overdub_bars,
            build_env=config.build_env,
            capture_serial_port=config.capture_serial_port,
            session_log=session_log,
        )
        print(f"[hitl-capture] run metadata: {path}", flush=True)
    except OSError as exc:
        print(f"[hitl-capture] metadata.json not written: {exc}", flush=True)


def legacy_args_with_managed_capture(
    legacy: list[str], args: object, session_log: Optional[Path] = None
) -> list[str]:
    if getattr(args, "verify_only", False):
        return legacy
    if getattr(args, "no_managed_capture", False):
        return legacy
    if "--serial-port" in legacy:
        return legacy
    merged = list(legacy)
    if session_log is not None:
        # Bind this run to the file actually opened, never a pointer in another
        # output directory or a previous run's explicit follow argument.
        filtered: list[str] = []
        index = 0
        while index < len(merged):
            value = merged[index]
            index += 1
            if value == "--follow-current-session":
                continue
            if value == "--follow-serial-log":
                index += 1
                continue
            if value.startswith("--follow-serial-log="):
                continue
            filtered.append(value)
        return filtered + ["--follow-serial-log", str(session_log)]
    if "--follow-current-session" not in merged:
        merged.append("--follow-current-session")
    return merged
