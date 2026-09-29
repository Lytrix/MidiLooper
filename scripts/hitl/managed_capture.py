"""Spawn capture_session.py alongside HITL (managed dual-session runs)."""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import threading
import time
import uuid
from contextlib import contextmanager
from dataclasses import replace
from pathlib import Path
from typing import Callable, Iterator, Optional

from hitl.config import DEFAULT_CAPTURE_SERIAL_PORT, HitlConfig
from hitl.serial.protocol import (collect_library_observation, fenced_rows,
                                  fenced_window, format_command_request, record_evidence_stats,
                                  record_evidence_row_counts, stopped_capture_queue_idle)

_BOOT_BANNER_MARKER = "DIAG,boot_reset,"

_CAPTURE_SCRIPT = Path(__file__).resolve().parent.parent / "capture_session.py"


def resolve_boot_reset_mode(value) -> str:
    """Normalize the boot mode (or the legacy boolean) through the capture's own definition.

    The capture owns the meaning of the modes: it is the process that asks the device whether it is
    already in the right state, so the harness asks it rather than restating the rule.
    """
    script_dir = str(_CAPTURE_SCRIPT.parent)
    if script_dir not in sys.path:
        sys.path.insert(0, script_dir)
    import capture_session

    return capture_session.normalize_boot_reset_mode(value)


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
    stale marker from an earlier run cannot authorize the new scenario, and the
    capture writes it as soon as it has evidence the device is up -- so this wait
    is normally a few seconds, and its timeout only bounds a device that never
    reaches a usable state.
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
        boot_reset: bool | str = "auto",
    ) -> None:
        self._port = port
        self._boot_wait_s = max(boot_wait_s, 0.0)
        self._out_dir = out_dir
        self._python = python_executable or sys.executable
        self._arm_snapshot_yield = arm_snapshot_yield
        self._run_token = run_token
        self._boot_reset_mode = resolve_boot_reset_mode(boot_reset)
        self._restarted_for_boot = False
        self._process: subprocess.Popen[str] | None = None
        self._output_thread: threading.Thread | None = None
        self._session_log: Path | None = None
        self._owner_write_fd: int | None = None
        # One outstanding read-only request at a time: the capture session is the
        # single serial owner, so overlap would make a response window ambiguous.
        self._pending_command_nonce: Optional[str] = None

    @property
    def session_log(self) -> Path | None:
        return self._session_log

    @property
    def boot_reset_mode(self) -> str:
        return self._boot_reset_mode

    def boot_banner_seen(self) -> bool:
        """Whether the capture that is running now observed its own boot banner."""
        log = self._session_log
        if log is None or not log.is_file():
            return False
        try:
            return _BOOT_BANNER_MARKER in log.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return False

    def restart_with_boot_reset(self) -> bool:
        """Own a fresh boot after the state check cleared the transport gate.

        `!REBOOT` is refused while the transport runs, so this recovers exactly the attach that
        failed because the device was left playing: the caller stops the transport once (idempotent
        MIDI Stop) and then asks for this restart, which is the same capture owner with a new
        session log and the boot banner inside it. Bounded to one restart per run.
        """
        if self._boot_reset_mode == "force":
            return False
        if self._restarted_for_boot:
            return False
        self._restarted_for_boot = True
        print("[hitl-capture] re-attaching with a forced boot reset", flush=True)
        self.stop()
        self._boot_reset_mode = "force"
        try:
            self.start()
        except TimeoutError as exc:
            print(f"[hitl-capture] fresh boot did not settle: {exc}", flush=True)
            return False
        return True

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
        # Allowlisted, fenced read-only requests on stdin (task 2.3).
        command.append("--command-stdin")
        # The capture decides the reset from the device's own state; the mode is what says whether
        # it may reset at all, so a run that only needs a driveable device never pays for a boot.
        command += ["--boot-reset-mode", self._boot_reset_mode]
        # Parent-death pipe: the child exits when this process closes the write
        # end, which happens even if the harness is SIGKILLed. That guarantees a
        # leaked capture session can never keep the USB serial port open.
        owner_read_fd, owner_write_fd = os.pipe()
        self._owner_write_fd = owner_write_fd
        command += ["--owner-fd", str(owner_read_fd)]
        if self._boot_wait_s > 0:
            print(
                f"[hitl-capture] starting capture_session.py on {self._port} "
                f"(boot mode {self._boot_reset_mode}, settle ceiling {self._boot_wait_s:.1f}s)",
                flush=True,
            )
        else:
            print(
                f"[hitl-capture] starting capture_session.py on {self._port} "
                f"(boot mode {self._boot_reset_mode})",
                flush=True,
            )
        started_at = time.monotonic()
        try:
            self._process = subprocess.Popen(
                command,
                stdin=subprocess.PIPE,
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

    def send_command(self, command: str, *, nonce: str, end_nonce: str) -> tuple[bool, str]:
        """Queue one allowlisted read-only request for the capture owner.

        Refuses while another request is outstanding, and refuses when no capture
        session owns the port, so a request can never be written by the wrong owner.
        """
        if self._pending_command_nonce is not None:
            return False, "one outstanding request"
        process = self._process
        if process is None or process.poll() is not None or process.stdin is None:
            return False, "capture session not running"
        try:
            process.stdin.write(format_command_request(command, nonce, end_nonce))
            process.stdin.flush()
        except (BrokenPipeError, ValueError, OSError) as exc:
            return False, f"write failed: {exc}"
        self._pending_command_nonce = nonce
        return True, "queued"

    def complete_command(self, nonce: str) -> None:
        """Release the outstanding-request slot for `nonce` (ack, timeout or loss)."""
        if self._pending_command_nonce == nonce:
            self._pending_command_nonce = None

    @property
    def pending_command_nonce(self) -> Optional[str]:
        return self._pending_command_nonce

    def fenced_response(self, nonce: str, end_nonce: str, *, timeout_s: float = 20.0,
                        poll_s: float = 0.05) -> Optional[list[str]]:
        """This request's fenced response window, or None when it never completed."""
        deadline = time.monotonic() + max(timeout_s, 0.0)
        try:
            while time.monotonic() < deadline:
                log = self._session_log
                if log is not None and log.is_file():
                    rows = fenced_rows(
                        log.read_text(encoding="utf-8", errors="replace").splitlines(),
                        nonce, end_nonce)
                    if rows is not None:
                        return rows
                time.sleep(poll_s)
            return None
        finally:
            self.complete_command(nonce)

    def drain_capture_ring(self) -> bool:
        """Ask the owner to flush the capture ring. Read-only and self-framed.

        The capture owner forwards only *allowlisted, fenced* requests: a bare
        command is refused as `malformed request` and never reaches the device, so
        a completed observation could stay invisible. The drain therefore goes out
        as a normal request line with its own fresh fence tokens. It is read-only,
        so it does not consume or require the outstanding-request slot; the
        observation's fenced window ignores these tokens.
        """
        process = self._process
        if process is None or process.poll() is not None or process.stdin is None:
            return False
        nonce, end_nonce = uuid.uuid4().hex, uuid.uuid4().hex
        try:
            process.stdin.write(format_command_request("!TRACE_DRAIN", nonce, end_nonce))
            process.stdin.flush()
        except (BrokenPipeError, ValueError, OSError):
            return False
        return True

    def drain_record_evidence(self, send_transport_stop: Callable[[], None], *,
                              timeout_s: float = 30.0) -> dict:
        """End-of-leg retrieval, while the same serial owner and MIDI port are open.

        Stop is idempotent MIDI Stop, not the control surface's transport toggle.
        A gated stats reply confirms stopped; fence-only replies do not. Completion
        concerns these three Record families, not persistence or the entire ring.
        """
        result = {"complete": False, "reason": "stopped stats unavailable", "drains": 0,
                  "stopped_confirmed": False, "families": {}, "log_counts": {}}
        deadline = time.monotonic() + max(timeout_s, 0.0)
        previous = None
        drain_start = None
        drain_start_counts = None
        reply_prefix = []

        def log_lines():
            return (self._session_log.read_text(encoding="utf-8", errors="replace").splitlines()
                    if self._session_log else [])

        def request(command):
            nonlocal reply_prefix
            nonce, end_nonce = uuid.uuid4().hex, uuid.uuid4().hex
            accepted, detail = self.send_command(command, nonce=nonce, end_nonce=end_nonce)
            if not accepted:
                result["reason"] = f"{command}: {detail}"
                return None
            rows = self.fenced_response(nonce, end_nonce,
                                        timeout_s=min(3.0, max(0.0, deadline - time.monotonic())))
            if rows is None:
                result["reason"] = f"{command}: response fence missing"
                return None
            lines = log_lines()
            window = fenced_window(lines, nonce, end_nonce)
            if window is None:
                result["reason"] = f"{command}: response log changed"
                return None
            # Match counters to exactly their response endpoint. Rows flushed after
            # that fence belong to the next snapshot, not this one's tx count.
            reply_prefix = lines[:window[1] + 1]
            return rows

        try:
            send_transport_stop()
            while time.monotonic() < deadline:
                # Check the selected finalizer before taking the counters: otherwise
                # it could emit its last rows between a stats snapshot and idle receipt.
                status = request("!STOP_STATUS")
                if status is None:
                    return result
                idle = stopped_capture_queue_idle(status)
                rows = request("!CAP_IDENTITY_STATS")
                if rows is None:
                    return result
                stats = record_evidence_stats(rows)
                if stats is None:
                    # Stop may still be queued when the first serial command runs.
                    result["reason"] = "stopped stats unavailable or incomplete"
                    time.sleep(min(0.05, max(0.0, deadline - time.monotonic())))
                    continue
                result["stopped_confirmed"] = True
                result["families"] = stats
                for family, counts in stats.items():
                    if any(value >= 0xFFFFFFFF for value in counts.values()):
                        result["reason"] = f"{family}: saturated counters"
                        return result
                    if previous and any(counts[key] < previous[family][key] for key in counts):
                        result["reason"] = f"{family}: counters regressed (possible reset)"
                        return result
                    if counts["rej"] or counts["evict"]:
                        result["reason"] = f"{family}: rejected or evicted evidence"
                        return result
                    if counts["entered"] != counts["enq"] or counts["tx"] > counts["enq"]:
                        result["reason"] = f"{family}: inconsistent counters"
                        return result
                previous = stats
                if drain_start is None:
                    # Counters span the boot, while this log can start later. Calibrate
                    # retrieval here; earlier leg receipts remain the baseline verifier's
                    # responsibility. Never require a reboot to zero historical tx counts.
                    drain_start = stats
                    drain_start_counts = record_evidence_row_counts(reply_prefix)
                    result["before"] = stats
                if idle and all(row["tx"] == row["enq"] for row in stats.values()):
                    observed = record_evidence_row_counts(reply_prefix)
                    result["log_counts"] = observed
                    if any(observed[family] - drain_start_counts[family] !=
                           stats[family]["tx"] - drain_start[family]["tx"] for family in stats):
                        result["reason"] = "newly transmitted Record rows do not match this session log"
                        return result
                    result.update(complete=True, reason="queued Record rows drained into this session log")
                    return result
                if request("!TRACE_DRAIN") is None:
                    return result
                result["drains"] += 1
                result["reason"] = "Record rows or selected stop queue still pending at drain deadline"
            return result
        except (OSError, ValueError, RuntimeError) as exc:
            result["reason"] = f"Record evidence drain failed: {exc}"
            return result

    def library_observation(self, nonce: str, end_nonce: str, *, timeout_s: float = 40.0,
                            poll_s: float = 0.05, drain_every_s: float = 2.0):
        """This request's complete library-observation family, or None.

        The command fence proves the request reached the device; the family's own
        `LIBOBS,E` row proves the census settled. Either one missing returns None.
        """
        deadline = time.monotonic() + max(timeout_s, 0.0)
        next_drain = time.monotonic()
        try:
            while time.monotonic() < deadline:
                log = self._session_log
                if log is not None and log.is_file():
                    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
                    window = fenced_window(lines, nonce, end_nonce)
                    if window is not None:
                        family = collect_library_observation(lines, window[1] + 1)
                        if family is not None and family.closed:
                            return family
                if drain_every_s > 0 and time.monotonic() >= next_drain:
                    self.drain_capture_ring()
                    next_drain = time.monotonic() + drain_every_s
                time.sleep(poll_s)
            return None
        finally:
            self.complete_command(nonce)

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
        # A managed run normally owns a real boot: the accepted-sequence oracle's fresh-boot gate
        # reads `#CAP,<us>,DIAG,boot_reset,` out of the capture, and a capture that attaches to an
        # already running device has no banner at all (`captures/s4f22-base-device-11` had none, so
        # that run could not be read as verdict-grade even though its record leg verified).
        # `capture_session.py` asks the firmware for the reset before its boot settle, so the banner
        # lands in this log. A repeatability campaign opts out (`--no-boot-reset`) because three
        # consecutive attempts on one boot is what the acceptance contract asks for; only the first
        # attempt of such a campaign owns the boot.
        boot_reset=config.boot_reset,
    )
    try:
        log_path = session.start()
        yield log_path
    finally:
        session.stop()


def attach_stream_is_usable(
    session_log: Path, *, min_lines: int = 40, require_stream: bool = True
) -> tuple[bool, str]:
    """Whether a capture that just attached has a usable device stream.

    A capture that owns the boot can attach to a device that is silent or wedged -- most often
    after an abruptly stopped previous session. The scenario then runs its whole measured
    sequence against nothing and reports a confusing `SPARSE_CAPTURE`
    (`captures/s4f22-step7-d1`: 15 lines, no `#CAP` row, `!STOP_STATUS` fence missing,
    `clear_to_empty: clear_not_confirmed`). Such a run must refuse instead of spending the
    sequence on it.

    An **attaching** run is different: it deliberately joins a *running, idle* device, and an
    idle device emits nothing until it is spoken to, so line volume says nothing about health
    (`captures/s4f22-step10-g1`: 21 lines and a perfectly responsive device, refused by the
    volume rule). Pass ``require_stream=False`` there -- readiness is then the fenced reply
    (`establish_attach_readiness`), and the count stays diagnostics.
    """
    try:
        text = session_log.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return False, f"session log unreadable: {exc}"
    lines = [line for line in text.splitlines() if line.strip()]
    if not require_stream:
        return True, f"{len(lines)} line(s), attaching to a running device"
    if len(lines) < min_lines:
        return False, f"device stream has {len(lines)} line(s); expected at least {min_lines}"
    if not any(line.startswith("#CAP,") or line.startswith("#") for line in lines):
        return False, "device stream carries no capture rows"
    return True, f"{len(lines)} line(s)"


def establish_attach_readiness(
    *, timeout_s: float = 12.0, send_transport_stop: Optional[Callable[[], None]] = None,
    stop_retries: int = 1,
) -> tuple[bool, str]:
    """Readiness from a fresh fenced stopped-state reply, never from line count.

    Line count says only that bytes arrived; it cannot separate a live device from a backlog left
    by an abruptly stopped session, and it accepts any `#`-prefixed noise. What readiness needs is
    a reply this run asked for: a fenced `!STOP_STATUS` whose rows appear **after** the attach,
    reporting the selected track stopped, with no pending finalization and an empty stop queue
    (`stopped_capture_queue_idle`). Persistence prerequisites stay with the caller's own save-idle
    wait, which is the owner of that contract.

    `!STOP_STATUS` is transport-gated, so a device whose transport is still running answers
    nothing -- and a run that was killed mid-sequence leaves exactly that state
    (`captures/s4f22-step10-g2`). The reply's silence is therefore ambiguous between "stopped
    transport, dead device" and "device fine, transport running", and `send_transport_stop` (one
    idempotent MIDI Stop, never the control surface's toggle) resolves it: after the stop a healthy
    device answers. A silent device is then refused by name, once.
    """
    session = active_managed_capture()
    if session is None:
        return True, "no managed capture (readiness not applicable)"
    log_path = session.session_log
    if log_path is None:
        return False, "managed capture has no session log"

    def read_lines() -> list[str]:
        try:
            return log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError as exc:
            raise RuntimeError(f"session log unreadable: {exc}") from exc

    try:
        baseline = len(read_lines())
        deadline = time.monotonic() + max(timeout_s, 0.0)
        last = "no reply"
        stops_sent = 0
        while time.monotonic() < deadline:
            nonce, end_nonce = uuid.uuid4().hex, uuid.uuid4().hex
            accepted, detail = session.send_command("!STOP_STATUS", nonce=nonce, end_nonce=end_nonce)
            if not accepted:
                return False, f"!STOP_STATUS refused: {detail}"
            # A direct-serial reply arrives in milliseconds; a transport-gated silence is then cheap
            # (1 s per attempt) instead of costing the whole readiness budget before the Stop.
            rows = session.fenced_response(
                nonce, end_nonce, timeout_s=min(1.0, max(0.1, deadline - time.monotonic()))
            )
            if rows is None:
                if send_transport_stop is not None and stops_sent < stop_retries:
                    stops_sent += 1
                    print(
                        "[hitl] no !STOP_STATUS reply; the transport is probably still running "
                        "from a killed run — sending one idempotent MIDI Stop and probing again",
                        flush=True,
                    )
                    send_transport_stop()
                    last = "no reply to the readiness probe; stopped the transport once"
                    continue
                last = (
                    "no !STOP_STATUS reply even after one idempotent MIDI Stop; the device is "
                    "not answering"
                    if stops_sent
                    else "no fenced !STOP_STATUS reply"
                )
                continue
            lines = read_lines()
            window = fenced_window(lines, nonce, end_nonce)
            if window is None:
                last = "reply not present in the session log"
                continue
            if window[1] <= baseline:
                last = "reply is not fresh (it precedes this attach)"
                continue
            status_rows = [row for row in rows if "#STOP_STATUS," in row]
            if not status_rows:
                last = "reply carried no #STOP_STATUS row"
                continue
            row = status_rows[-1]
            if "state=STOPPED" not in row:
                last = f"device is not stopped: {row.strip()}"
                continue
            if not stopped_capture_queue_idle([row]):
                last = f"stop queue is not idle: {row.strip()}"
                continue
            suffix = " after one idempotent MIDI Stop" if stops_sent else ""
            return True, f"fresh stopped reply with an idle stop queue{suffix}"
        return False, last
    except (OSError, ValueError, RuntimeError) as exc:
        return False, f"readiness probe failed: {exc}"


def transport_stop_sender(config: HitlConfig, sent: dict) -> Callable[[], None]:
    """One idempotent MIDI Stop to the configured output, at most once per run.

    Deliberately not the control surface's transport press: that is a *toggle*, so a blind press
    can start the transport it was meant to stop. `mido.Message("stop")` is the same idempotent
    stop the end-of-leg Record evidence drain already sends, and the firmware routes it to
    `handleMidiStop()`, which clears `sequencerRunning` -- including the gate that refuses
    `!REBOOT` while the transport runs.

    `config.midi_out_name` is a *substring* of the port name (`"Teensy"` for the ALSA/CoreMIDI port
    "Teensy MIDI"), resolved with the same helper the scenario uses -- opening it verbatim failed
    the first device run of this slice (`captures/s4f22-step11-h1`: `unknown port 'Teensy'`).
    """
    def _send() -> None:
        if sent.get("value"):
            return
        sent["value"] = True
        try:
            import mido

            from hitl.midi_io import _find_midi_port

            port_name = _find_midi_port(config.midi_out_name, is_input=False)
            with mido.open_output(port_name) as port:
                port.send(mido.Message("stop"))
            print(f"[hitl] sent one idempotent MIDI Stop to {port_name!r}", flush=True)
        except Exception as exc:  # noqa: BLE001 - a missing MIDI port is reported, not fatal
            print(f"[hitl] could not send the MIDI Stop: {exc}", flush=True)

    return _send


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

    Before the scenario, the device's state decides the boot: a device the capture
    confirmed stopped is driven straight away, and one that cannot answer the
    readiness probe is recovered with one idempotent MIDI Stop (a killed run leaves
    the transport running, which is also what makes the firmware refuse `!REBOOT`).
    A run that wanted a fresh boot and still has no banner then re-attaches with a
    forced reset, now that the transport gate is clear.
    """
    timeout = resolve_managed_run_timeout(timeout_s)
    with managed_capture_session(config) as log_path:
        mode = resolve_boot_reset_mode(config.boot_reset)
        stopped_once = {"value": False}
        # Line count is diagnostic only; readiness comes from a reply this run asked for. Only a
        # run that forces the boot promises a fresh stream to count.
        usable, detail = attach_stream_is_usable(log_path, require_stream=(mode == "force"))
        print(f"[hitl] attach stream: {detail}")
        if not usable:
            print(f"[hitl] attached stream is not usable ({detail}); refusing to run the scenario")
            write_metadata_best_effort(config, session_log=log_path)
            return 3
        ready, reason = establish_attach_readiness(
            send_transport_stop=transport_stop_sender(config, stopped_once)
        )
        print(f"[hitl] attach readiness: {reason}")
        # A ready device is driven straight away (no reboot, no boot wait). A device this run had to
        # *repair* is a different case: the idempotent Stop changed the state, and the capture stream
        # now mixes the ring's stale backlog with fresh rows -- the drain lands old `BAR`/`DFRAME`
        # rows *after* newer ones, so a scenario that reads "is the transport running?" from line
        # position sees the transport running (`captures/s4f22-step11-h2`: BAR rows stamped 850-1864 s
        # interleaved after a 1855 s `state=STOPPED` reply, and the scenario's stop press then
        # *started* the transport, so the clear could not drain). Own a fresh boot for that case: it
        # is the state the scenario is written against, and the reboot clears the backlog with it.
        needs_fresh_boot = mode != "never" and (not ready or stopped_once["value"])
        if needs_fresh_boot:
            session = active_managed_capture()
            if session is not None and not session.boot_banner_seen() \
                    and session.restart_with_boot_reset():
                log_path = session.session_log or log_path
                usable, detail = attach_stream_is_usable(log_path, require_stream=True)
                print(f"[hitl] fresh-boot attach stream: {detail}")
                if usable:
                    ready, reason = establish_attach_readiness()
                    print(f"[hitl] fresh-boot readiness: {reason}")
        if not ready:
            print(f"[hitl] attach readiness failed ({reason}); refusing to run the scenario")
            write_metadata_best_effort(config, session_log=log_path)
            return 3
        print(f"[hitl] attach readiness: {reason}")
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
