"""Spawn capture_session.py alongside HITL (managed dual-session runs)."""

from __future__ import annotations

import json
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

from hitl.actions.cancellation import active_cancel_scope, cancel_active_run

from hitl.config import DEFAULT_CAPTURE_SERIAL_PORT, HitlConfig
from hitl.serial.protocol import (collect_library_observation, fenced_rows,
                                  fenced_window, format_command_request, record_evidence_stats,
                                  record_evidence_row_counts, ring_loss_counters,
                                  stopped_capture_queue_idle)

_BOOT_BANNER_MARKER = "DIAG,boot_reset,"

# A fenced request needs room for its own answer: with less than this left, the retrieval stops
# asking instead of reporting a fence it never waited for. Two extra attempts cover a ring flush
# whose fence was genuinely lost; the flush is read-only, so re-asking cannot duplicate evidence.
_MIN_REQUEST_BUDGET_S = 0.06
_DRAIN_FENCE_RETRIES = 2

# A family shortfall (`enq - tx`) unchanged across this many consecutive snapshots is frozen rather
# than pending: a row the ring shed before this log opened, not one still waiting to be transmitted.
_DRAIN_STALL_ROUNDS = 2

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

# How long a cancelled scenario is given to leave the wire before the capture child is released. Bounded on
# purpose: an abandoned run must not hold the capture, and its sends are refused once it is cancelled anyway.
DEFAULT_CANCEL_GRACE_S = 30.0
_CANCEL_GRACE_ENV = "HITL_CANCEL_GRACE_S"


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


def resolve_cancel_grace(grace_s: Optional[float] = None) -> float:
    """Resolve how long a cancelled scenario may take to stop before the capture is released."""
    if grace_s is not None:
        return max(float(grace_s), 0.0)
    raw = os.environ.get(_CANCEL_GRACE_ENV)
    if raw:
        try:
            return max(float(raw), 0.0)
        except ValueError:
            pass
    return DEFAULT_CANCEL_GRACE_S


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

    def restart_with_boot_reset(self, stop_transport: Optional[Callable[[], None]] = None) -> bool:
        """Own a fresh boot after the state check cleared the transport gate.

        `!REBOOT` is refused while the transport runs, so this recovers exactly the attach that
        failed because the device was left playing: the caller hands in the one idempotent MIDI Stop
        (never the control surface's toggle) that clears that gate, and the restart is the same
        capture owner with a new session log and the boot banner inside it. Bounded to one restart
        per run, and a capture that already carries its own boot banner is not restarted.

        The Stop belongs *here*, not in readiness: a run that asked for a fresh boot has already
        decided to discard the device's playback, while a plain attach must not stop the transport
        the scenario is about to measure (`captures/live-current-root-entry-t8s4-07/08/09`). Both
        contracts hold because only the boot-bound path hands the sender in.
        """
        if self._restarted_for_boot or self.boot_banner_seen():
            return False
        if stop_transport is not None:
            stop_transport()
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
            # Read before declaring timeout: a complete response may have landed
            # during the final poll sleep (notably the 0.5s live status query).
            while True:
                log = self._session_log
                if log is not None and log.is_file():
                    rows = fenced_rows(
                        log.read_text(encoding="utf-8", errors="replace").splitlines(),
                        nonce, end_nonce)
                    if rows is not None:
                        return rows
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                time.sleep(min(poll_s, remaining))
        finally:
            self.complete_command(nonce)

    def identity_stat_lines(self, *, timeout_s: float = 6.0) -> Optional[list[str]]:
        """The stopped-only per-family counter read-back rows, or None when the session cannot answer.

        The single owner of this conversation: the request, its fence and the decode stay with the serial
        owner (`hitl.serial.protocol`), because neither a flow nor a scenario may import that layer and two
        callers asking the same question must not each grow their own parser. Read-only and non-fatal -- a
        session that cannot answer costs the accounting evidence, never the run.
        """
        nonce, end_nonce = uuid.uuid4().hex, uuid.uuid4().hex
        try:
            accepted, _detail = self.send_command("!CAP_IDENTITY_STATS",
                                                  nonce=nonce, end_nonce=end_nonce)
            if not accepted:
                return None
            return self.fenced_response(nonce, end_nonce, timeout_s=timeout_s)
        except Exception:  # a read-back must never be the thing that fails a run
            return None

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

    def selected_track_status(self, *, timeout_s: float = 0.5) -> Optional[dict]:
        """Fresh selected-track scalar observation; never infer it from a retained ring row."""
        from hitl.serial.protocol import selected_track_status
        nonce, end_nonce = uuid.uuid4().hex, uuid.uuid4().hex
        accepted, _detail = self.send_command('!TRACK_STATUS', nonce=nonce, end_nonce=end_nonce)
        if not accepted:
            return None
        rows = self.fenced_response(nonce, end_nonce, timeout_s=max(0.0, timeout_s))
        return selected_track_status(rows or [])

    def drain_record_evidence(self, send_transport_stop: Callable[[], None], *,
                              timeout_s: float = 30.0) -> dict:
        """End-of-leg retrieval, while the same serial owner and MIDI port are open.

        Stop is idempotent MIDI Stop, not the control surface's transport toggle.
        A gated stats reply confirms stopped; fence-only replies do not. Completion
        concerns these three Record families, not persistence or the entire ring.

        The counters in that reply span the boot, while this retrieval can only judge
        what the ring is still holding now. Completion therefore waits for the selected
        stop queue to idle and for the per-family shortfall (`enq - tx`) to *stop
        shrinking*, instead of a boot-global `tx == enq` that a row shed before this log
        opened can never satisfy: `captures/live-current-root-entry-t8s4-14` held
        `RID entered=7,enq=7,tx=6` (and the same for `RECA`) for the rest of that boot, so
        the strict equality drained for the whole deadline (`drains=20`) and reported
        `SERIAL_INCOMPLETE` for evidence the leg had already received. A shortfall this
        retrieval *witnesses* still fails closed -- a growing shortfall, a family
        `rej`/`evict`, a regressed or saturated counter, and rows transmitted without
        appearing in this log all remain incomplete -- and a frozen remainder is named in
        `stalled_families` next to the ring's own loss counters, so a reader can see it was
        not this session's rows.

        A fenced request needs room for its own answer: nothing is sent with less than
        `_MIN_REQUEST_BUDGET_S` of the deadline left, so a reply that simply arrived after
        the retrieval's budget ran out is reported as pending-at-deadline rather than as a
        lost fence (that mis-report is what aborted the run above), and the idempotent ring
        flush is retried a bounded number of times when its fence is genuinely missing.
        """
        result = {"complete": False, "reason": "stopped stats unavailable", "drains": 0,
                  "stopped_confirmed": False, "families": {}, "log_counts": {},
                  "drain_retries": 0, "stalled_families": {}, "ring_loss": {}}
        deadline = time.monotonic() + max(timeout_s, 0.0)
        previous = None
        previous_shortfall = None
        frozen_rounds = 0
        drain_start = None
        drain_start_counts = None
        reply_prefix = []

        def log_lines():
            return (self._session_log.read_text(encoding="utf-8", errors="replace").splitlines()
                    if self._session_log else [])

        def budget() -> float:
            return max(0.0, deadline - time.monotonic())

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

        def flush_ring():
            """The idempotent ring flush, retried while its fence is missing and time remains.

            `None` means the flush did not complete: either the request could not be asked within the
            remaining budget (the pending-deadline reason stands) or its fence was lost.
            """
            if budget() < _MIN_REQUEST_BUDGET_S:
                return None
            rows = request("!TRACE_DRAIN")
            while rows is None and budget() >= _MIN_REQUEST_BUDGET_S:
                if result["drain_retries"] >= _DRAIN_FENCE_RETRIES:
                    break
                result["drain_retries"] += 1
                rows = request("!TRACE_DRAIN")
            return rows

        try:
            send_transport_stop()
            while time.monotonic() < deadline:
                if budget() < _MIN_REQUEST_BUDGET_S:
                    # Too little left to ask a question and read its answer: report what was
                    # already retrieved instead of claiming a fence this retrieval never waited for.
                    return result
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
                result["ring_loss"] = ring_loss_counters(rows)
                result["reason"] = "Record rows or selected stop queue still pending at drain deadline"
                for family, counts in stats.items():
                    if any(value >= 0xFFFFFFFF for value in counts.values()):
                        result["reason"] = f"{family}: saturated counters"
                        return result
                    # `previous` is this retrieval's own earlier snapshot; a family it did not carry (an
                    # older reply shape) has no baseline to regress against, so it is compared only with itself.
                    previous_family = previous.get(family) if previous else None
                    if previous_family and any(counts[key] < previous_family[key] for key in counts):
                        result["reason"] = f"{family}: counters regressed (possible reset)"
                        return result
                    if counts["rej"] or counts["evict"]:
                        result["reason"] = f"{family}: rejected or evicted evidence"
                        return result
                    if counts["entered"] != counts["enq"] or counts["tx"] > counts["enq"]:
                        result["reason"] = f"{family}: inconsistent counters"
                        return result
                shortfall = {family: counts["enq"] - counts["tx"]
                             for family, counts in stats.items() if counts["enq"] > counts["tx"]}
                frozen_rounds = frozen_rounds + 1 if shortfall == previous_shortfall else 0
                previous_shortfall = shortfall
                previous = stats
                if drain_start is None:
                    # Counters span the boot, while this log can start later. Calibrate
                    # retrieval here; earlier leg receipts remain the baseline verifier's
                    # responsibility. Never require a reboot to zero historical tx counts.
                    drain_start = stats
                    drain_start_counts = record_evidence_row_counts(reply_prefix)
                    result["before"] = stats
                if idle and (not shortfall or frozen_rounds >= _DRAIN_STALL_ROUNDS):
                    observed = record_evidence_row_counts(reply_prefix)
                    result["log_counts"] = observed
                    result["stalled_families"] = shortfall
                    # The reply carries every counted family, but only the families whose rows the log
                    # counter sees have a row counterpart to reconcile against: `RECA`/`RECS`/`RID`
                    # receipts. A family that is *reported* rather than counted -- `DIAG,latch`, task 2.2's
                    # latch evidence -- is cross-checked by its own consistency rule above and is named here
                    # instead of being dropped. Iterating `stats` directly raised `KeyError: DIAG,latch` and
                    # turned a clean baseline run into `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`.
                    reconcile = [family for family in stats if family in observed]
                    result["reconciled_families"] = sorted(reconcile)
                    result["unreconciled_families"] = sorted(set(stats) - set(reconcile))
                    if any(observed[family] - drain_start_counts[family] !=
                           stats[family]["tx"] - drain_start[family]["tx"] for family in reconcile):
                        result["reason"] = "newly transmitted Record rows do not match this session log"
                        return result
                    reason = "queued Record rows drained into this session log"
                    if shortfall:
                        reason += ("; " + ", ".join(f"{family}={rows_}" for family, rows_ in sorted(shortfall.items()))
                                   + " Record row(s) never left the ring (not from this session)")
                    result.update(complete=True, reason=reason)
                    return result
                if flush_ring() is None:
                    return result
                result["drains"] += 1
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


def stop_transport_and_observe_heap(
    session: "ManagedCaptureSession", *, midi_out_name: str = "Teensy", timeout_s: float = 8.0
) -> list[str]:
    """Run end: stop the transport, **then** ask for the stopped-state heap observation.

    The order is the point. Mid-run the HITL keeps the device playing and never presses
    (`establish_attach_readiness` proves liveness from the rows a playing device emits); the run's
    own end is where it should be stopped, and `!HEAP_STATS` is only granted once it is -- with the
    transport running, no allowlisted query answers at all (measured). The idempotent realtime Stop
    is used rather than the control surface's press, which is a toggle.
    """
    try:
        import uuid

        import mido

        from hitl.midi_io import _find_midi_port

        out_port = mido.open_output(_find_midi_port(midi_out_name, "output"))
        try:
            out_port.send(mido.Message("stop"))
        finally:
            out_port.close()
        time.sleep(0.5)
        nonce, end_nonce = uuid.uuid4().hex, uuid.uuid4().hex
        accepted, detail = session.send_command("!HEAP_STATS", nonce=nonce, end_nonce=end_nonce)
        if not accepted:
            return [f"(heap observation not queued: {detail})"]
        rows = session.fenced_response(nonce, end_nonce, timeout_s=timeout_s)
        session.complete_command(nonce)
        if rows is None:
            return ["(no fenced !HEAP_STATS reply after the run-end stop)"]
        return list(rows)
    except Exception as exc:  # evidence, never a teardown failure
        return [f"(run-end heap observation failed: {exc!r})"]


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
        # Run end: the transport is stopped here, and only here, and the stopped-state heap
        # observation follows it. Mid-run the HITL keeps the device playing and never presses.
        heap_rows = stop_transport_and_observe_heap(
            session, midi_out_name=config.midi_out_name
        )
        for row in heap_rows:
            print(f"[hitl] run-end heap observation: {row.strip()}", flush=True)
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


#: Minimum fresh device rows an attaching run accepts as liveness proof when no command answers
#: (a playing device refuses every transport-gated query). Rows must carry device stamps newer
#: than this attach -- freshness, not volume.
_ATTACH_FRESH_ROW_FLOOR = 8


def establish_attach_readiness(
    *, timeout_s: float = 12.0, send_transport_stop: Optional[Callable[[], None]] = None,
    stop_retries: int = 1,
) -> tuple[bool, str]:
    """Readiness from a fresh reply this run asked for, never from line count.

    Line count says only that bytes arrived; it cannot separate a live device from a backlog left
    by an abruptly stopped session, and it accepts any `#`-prefixed noise. What readiness needs is
    a reply this run asked for, appearing **after** the attach.

    **The end goal is always playing, never stopping**, so this probe asks something the device
    answers while its transport runs. `!STOP_STATUS` cannot be it: every status query except
    `!LIB_OBSERVE` is transport-gated in firmware (`StorageManagerHitlSerial.cpp`), so a playing
    device replies to none of them -- which is why this step used to send an idempotent MIDI Stop
    first, and why the composed preset's second leg then measured its edit gestures on the stopped
    take the base leg had left playing (`captures/live-current-root-entry-t8s4-07/08/09`).
    `!HEAP_STATS` is the one allowlisted query with no transport gate, and it reports exactly the
    state readiness cares about (its own transport gate, whether any track is playing or has work
    in flight, and whether storage is busy), so it proves liveness *and* describes the device
    without touching it. `send_transport_stop` stays in the signature for callers that still pass
    one, but this function never presses: the run owns the device's state, not this probe.
    """
    from hitl.device_time import filter_live_lines

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
            accepted, detail = session.send_command("!HEAP_STATS", nonce=nonce, end_nonce=end_nonce)
            if not accepted:
                return False, f"!HEAP_STATS refused: {detail}"
            # A direct-serial reply arrives in milliseconds; the budget below only bounds a device
            # that stops answering at all.
            rows = session.fenced_response(
                nonce, end_nonce, timeout_s=min(1.5, max(0.1, deadline - time.monotonic()))
            )
            if rows is None:
                # No command answers while the transport runs: every status query is transport-
                # gated in firmware and `!LIB_OBSERVE`'s reply is ring-deferred behind a gated
                # drain (measured on this image: all three silent with the device playing). A
                # playing device is exactly the state the end goal asks for, so fall back to the
                # evidence it *is* emitting: rows with fresh device stamps since this attach.
                fresh_rows = filter_live_lines(read_lines(), after_index=baseline)
                if len(fresh_rows) >= _ATTACH_FRESH_ROW_FLOOR:
                    return True, (
                        f"no command replies while the transport runs; "
                        f"{len(fresh_rows)} fresh device row(s) since the attach"
                    )
                last = (
                    f"no fenced !HEAP_STATS reply and only {len(fresh_rows)} fresh device row(s)"
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
            stat_rows = [row for row in rows if "#HEAP_STATS," in row]
            if not stat_rows:
                last = "reply carried no #HEAP_STATS row"
                continue
            row = stat_rows[-1]
            # A refusal is still an answer: the device replied to a query this run asked, which is
            # what readiness needs. A *playing* device (the end goal) refuses the heap observation
            # by design, so refusing on `refused=` would make "always playing" un-attachable; the
            # reason travels with the verdict, and the reply's own gate fields are on that row. The
            # final stopped-state heap observation belongs to the run's own end, after the
            # transport is stopped there.
            return True, f"fresh heap observation: {row.strip()}"
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
        import mido
        from hitl.midi_io import _find_midi_port
        port_name = _find_midi_port(config.midi_out_name, is_input=False)
        with mido.open_output(port_name) as port:
            port.send(mido.Message('stop'))
        # Cache only a successful boot-repair Stop. Teardown failures must reach
        # DeviceSession, which records disconnection and latches against reset.
        sent['value'] = True
        print(f'[hitl] sent one idempotent MIDI Stop to {port_name!r}', flush=True)

    return _send


def _run_with_watchdog(run_fn: Callable[[], int], timeout_s: float,
                       *, grace_s: Optional[float] = None) -> int:
    """Run `run_fn` on a worker thread behind a wall-clock ceiling.

    Returns the run code, or 3 if the ceiling is exceeded. Exceptions from the
    worker are re-raised here so callers keep normal failure semantics.

    When the ceiling is exceeded the run is **cancelled first** and given a bounded grace to leave the wire,
    and only then does this return -- its caller's `managed_capture_session` context releases the capture child
    on the way out. That order is the point: a scenario still pressing while the capture is being released is
    the defect, and `hitl.actions.cancellation` guarantees it cannot press once cancelled. A scenario that will
    not stop is reported and abandoned rather than waited on forever, because an abandoned run must not hold
    the capture or the serial port.
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
        grace = resolve_cancel_grace(grace_s)
        scope = active_cancel_scope()
        cancelled = cancel_active_run(f"managed run watchdog {timeout_s:.1f}s")
        print(
            f"[hitl-capture] scenario exceeded {timeout_s:.1f}s watchdog; "
            + ("cancelling the run and giving it " if cancelled
               else "no run scope is registered; giving it " if scope is None
               else "the run was already cancelled; giving it ")
            + f"{grace:.1f}s to stop before the capture is released",
            flush=True,
        )
        if scope is None:
            # Nothing to cancel, and nothing that promised to stop: releasing and returning is the right
            # behaviour here, and it keeps the harness bounded.
            print("[hitl-capture] releasing the capture without waiting (no run scope to cancel)", flush=True)
            return 3
        worker.join(grace)
        if worker.is_alive():
            print(
                "[hitl-capture] the scenario did not stop after cancellation; releasing the capture anyway "
                "(its sends are refused from here on)",
                flush=True,
            )
        else:
            print("[hitl-capture] the scenario stopped after cancellation; releasing the capture", flush=True)
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
    from hitl.device_shutdown import DeviceSession
    timeout = resolve_managed_run_timeout(timeout_s)
    with managed_capture_session(config) as log_path:
        def run_attached() -> int:
            nonlocal log_path
            mode = resolve_boot_reset_mode(config.boot_reset)
            stopped_once = {'value': False}
            ready, reason = establish_attach_readiness(
                send_transport_stop=transport_stop_sender(config, stopped_once))
            print(f'[hitl] attach readiness: {reason}')
            needs_fresh_boot = mode == 'force' or (mode != 'never' and (not ready or stopped_once['value']))
            if needs_fresh_boot:
                session = active_managed_capture()
                if session is not None and session.restart_with_boot_reset(
                        transport_stop_sender(config, stopped_once)):
                    log_path = session.session_log or log_path
                    print('[hitl-capture] fresh boot owns the device state; the scenario starts from it')
                    ready, reason = establish_attach_readiness()
                    print(f'[hitl] fresh-boot readiness: {reason}')
            usable, detail = attach_stream_is_usable(log_path, require_stream=(mode == 'force'))
            print(f'[hitl] attach stream: {detail}')
            if not usable or not ready:
                print(f'[hitl] attach refused: {detail if not usable else reason}')
                write_metadata_best_effort(config, session_log=log_path)
                return 3
            active = replace(config, follow_current_session=False, follow_serial_log=log_path,
                             legacy_extra_args=tuple(legacy_args_with_managed_capture(
                                 list(config.legacy_extra_args), config, log_path)))
            write_metadata_best_effort(active, session_log=log_path)
            return _run_with_watchdog(lambda: run_fn(active), timeout)

        # Fresh sender state for EACH teardown cycle, independent of boot repair.
        # The outer capture remains open through cleanup, including raised/failed legs.
        shutdown = DeviceSession(capture_log_getter=lambda: log_path,
                                 send_transport_stop=lambda: transport_stop_sender(config, {})(),
                                 out_dir=config.out_dir, context='managed-hitl-run')
        try:
            with shutdown:
                code = run_attached()
        finally:
            config.out_dir.mkdir(parents=True, exist_ok=True)
            (config.out_dir / 'shutdown-result.json').write_text(
                json.dumps(shutdown.result.as_dict(), indent=2) + '\n', encoding='utf-8')
        if not shutdown.result.ok:
            print(f'[hitl] canonical shutdown incomplete: {shutdown.result.detail}')
            return code or 3
        return code


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
