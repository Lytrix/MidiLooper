#!/usr/bin/env python3
"""Canonical device shutdown, plus the latch that blocks an automatic reset after a bad one.

**Why this exists (2026-09-26 workspace-load failure).** A run cleared a slot, which marked it dirty and
admitted a deferred workspace save, and then returned immediately -- closing the capture session and the
serial port while that save was still writing. The next run's upload reset the board mid-write; boot hygiene
discarded the incomplete temp; the workspace then failed to load and the device came up empty. The session log
ends exactly as described: `PERS,work,...,TrackMeta,track:7,start,ok` with no completion.

The HITL skill already documents this failure mode ("serial log ends mid-write (e.g. mid ``PERS,work``)";
"a bespoke reader that closes right after ``stop`` reproduces this every time") and prescribes the canonical
protocol: **stop -> drain -> stop -> drain**, with the port held open through verified completion.

Two rules this module enforces, both learned the hard way:

* **The drain is verified, never slept.** Completion is decided by the existing owner of that question,
  `hitl.deferred_save_idle.wait_for_deferred_save_idle`. A fixed sleep cannot tell "saved" from "still
  writing" and is what makes the failure intermittent.
* **An incomplete shutdown is sticky.** It is reported as a distinct outcome and latched, so a follow-up
  tool refuses to upload/reset until a human clears it. The dangerous step is not the drain failing; it is
  the *next* reset arriving on top of a save that never finished.
"""

from __future__ import annotations

import json
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional

from hitl.deferred_save_idle import deferred_save_idle_in_tail  # noqa: E402

#: Repository root, so the latch is found regardless of the caller's cwd.
_REPO_ROOT = Path(__file__).resolve().parents[2]

#: Well-known latch: a session that ended with an incomplete device shutdown.
LATCH_PATH = _REPO_ROOT / "captures" / ".device-shutdown-incomplete.json"

COMPLETE = "complete"
INCOMPLETE_TIMEOUT = "incomplete_timeout"
INCOMPLETE_DISCONNECTED = "incomplete_disconnected"
NOT_ATTEMPTED = "not_attempted"


class LogCollector:
    """The ``collector`` surface ``wait_for_deferred_save_idle`` needs, over a session-log file.

    Deliberately re-reads the file on each snapshot: the drain decision must reflect what the device has
    actually written, not a cached prefix.
    """

    def __init__(self, path: Path | str) -> None:
        self._path = Path(path)

    def snapshot(self) -> list[str]:
        try:
            return self._path.read_text(errors="replace").splitlines()
        except OSError:
            return []


@dataclass
class ShutdownResult:
    status: str
    drains: int
    detail: str

    @property
    def ok(self) -> bool:
        return self.status == COMPLETE

    def as_dict(self) -> dict:
        #  `ok` travels with the record: it is this owner's own verdict, and the reader that vetoes a pass on an
        #  incomplete drain (`hitl.reporting.derive_verdict`) asks for exactly this key. Without it a *real* run's
        #  shutdown record could never satisfy that rule, however cleanly it drained.
        return {"status": self.status, "drains": self.drains, "detail": self.detail, "ok": self.ok}


def shutdown_device(
    *,
    capture_log: Path | str,
    send_transport_stop: Callable[[], None],
    drain_timeout_s: float = 30.0,
    quiet_s: float = 2.0,
    cycles: int = 2,
    is_connected: Optional[Callable[[], bool]] = None,
    tail_lookback: int = 200,
) -> ShutdownResult:
    """Stop -> drain -> stop -> drain, each drain verified from the log.

    ``send_transport_stop`` performs one transport press. A press that raises, or a device that reports
    itself disconnected, is a **disconnection** rather than a timeout -- both are incomplete, but naming them
    apart is what lets a later reader tell "the device went away" from "the save never finished".

    **The idle decision is taken from the log tail, never from "lines since I last looked".** A
    new-lines-only window is empty at the instant it is opened, and the idle owner correctly calls an empty
    suffix idle -- so anchoring at "now" reports success before the save has even been admitted. That
    vacuous pass was a real defect in the first cut of this module, caught by
    `test_the_incident_log_is_never_reported_complete`. `quiet_s` is a *debounce on a verified predicate*,
    not a fixed sleep: it covers the few hundred milliseconds between the stop press and the save being
    admitted, and any work appearing inside that window resets it.
    """
    collector = LogCollector(capture_log)
    if not collector.snapshot():
        return ShutdownResult(INCOMPLETE_TIMEOUT, 0,
                              "no serial output to verify persistence state against")
    drains = 0
    for cycle in range(max(cycles, 1)):
        if is_connected is not None and not is_connected():
            return ShutdownResult(INCOMPLETE_DISCONNECTED, drains,
                                  f"device disconnected before stop cycle {cycle + 1}")
        try:
            send_transport_stop()
        except Exception as exc:  # a closed port is a disconnection, never a pass
            return ShutdownResult(INCOMPLETE_DISCONNECTED, drains,
                                  f"transport stop press failed: {exc!r}")
        if not _wait_for_verified_idle(collector, timeout_s=drain_timeout_s, quiet_s=quiet_s,
                                       tail_lookback=tail_lookback):
            return ShutdownResult(
                INCOMPLETE_TIMEOUT, drains,
                f"persistence never reached verified idle after stop cycle {cycle + 1} "
                f"({drain_timeout_s:.0f}s cap) -- the device may still be writing a workspace save")
        drains += 1
    return ShutdownResult(COMPLETE, drains,
                          f"{drains} stop+drain cycle(s) reached verified persistence idle")


def _wait_for_verified_idle(collector: "LogCollector", *, timeout_s: float, quiet_s: float,
                            tail_lookback: int) -> bool:
    """True once the tail has shown persistence idle continuously for ``quiet_s``.

    Delegates the decision itself to `deferred_save_idle_in_tail` -- the existing owner of "is the deferred
    save idle" -- and adds only the stability requirement.
    """
    deadline = time.monotonic() + timeout_s
    idle_since: Optional[float] = None
    while True:
        now = time.monotonic()
        if deferred_save_idle_in_tail(collector.snapshot(), lookback=tail_lookback):
            if idle_since is None:
                idle_since = now
            if now - idle_since >= quiet_s:
                return True
        else:
            idle_since = None
        if now >= deadline:
            return False
        time.sleep(0.05)


def mark_incomplete(result: ShutdownResult, *, out_dir: Path | str | None = None,
                    context: str = "", latch_path: Path | str = LATCH_PATH) -> Path:
    """Latch an incomplete shutdown so a follow-up upload/reset is refused."""
    payload = dict(result.as_dict(), context=context, recorded_at=time.time())
    latch = Path(latch_path)
    latch.parent.mkdir(parents=True, exist_ok=True)
    latch.write_text(json.dumps(payload, indent=2) + "\n")
    if out_dir is not None:
        detail = Path(out_dir) / "shutdown-incomplete.json"
        detail.parent.mkdir(parents=True, exist_ok=True)
        detail.write_text(json.dumps(payload, indent=2) + "\n")
    return latch


def clear_latch(latch_path: Path | str = LATCH_PATH) -> None:
    """Clear the latch. Deliberate human action once the device is known good."""
    Path(latch_path).unlink(missing_ok=True)


def latch_reason(latch_path: Path | str = LATCH_PATH) -> Optional[str]:
    """The latched reason, or None when no incomplete shutdown is recorded."""
    latch = Path(latch_path)
    if not latch.exists():
        return None
    try:
        payload = json.loads(latch.read_text())
    except (OSError, ValueError):
        return "unreadable latch"
    return f"{payload.get('status')}: {payload.get('detail')}"


def refuse_reset_if_latched(*, tool: str, latch_path: Path | str = LATCH_PATH) -> Optional[str]:
    """Return a refusal message when a reset must not happen, else None.

    Call this **before** any upload/reset. The incident was not the interrupted save by itself; it was the
    next reset landing on top of it.
    """
    reason = latch_reason(latch_path)
    if reason is None:
        return None
    return (f"{tool}: refusing to upload/reset the device. A previous session ended with an incomplete "
            f"device shutdown ({reason}). Resetting now can interrupt a workspace save mid-write, which is "
            f"exactly the 2026-09-26 workspace-load failure. Recover deliberately -- see "
            f"openspec/changes/runtime-working-set-ownership/evidence/2026-09-26-workspace-loss/ -- then "
            f"clear {latch_path} once the device is known good.")


class DeviceSession:
    """Context manager that guarantees the canonical drain on **every** exit.

    This is the structural answer to the defect: rather than remembering to drain at each early return, the
    drain runs from ``__exit__``, so a normal return, an early return, and an exception are all covered. A
    per-exit helper can be forgotten when a new return is added; this cannot.
    """

    def __init__(self, *, capture_log_getter: Callable[[], Optional[Path | str]],
                 send_transport_stop: Callable[[], None], out_dir: Path | str,
                 context: str = "", events: Optional[list] = None,
                 drain_timeout_s: float = 30.0, is_connected: Optional[Callable[[], bool]] = None,
                 latch_path: Path | str = LATCH_PATH) -> None:
        self._get_log = capture_log_getter
        self._stop = send_transport_stop
        self._out_dir = out_dir
        self._context = context
        self._events = events if events is not None else []
        self._timeout = drain_timeout_s
        self._is_connected = is_connected
        self._latch_path = latch_path
        self.result = ShutdownResult(NOT_ATTEMPTED, 0, "session did not reach shutdown")

    def __enter__(self) -> "DeviceSession":
        return self

    def __exit__(self, exc_type, exc, tb) -> bool:
        log = self._get_log()
        if log is None:
            # No session log means the device was never opened far enough to need a drain; the session
            # itself will have failed loudly, so this is not reported as an incomplete *shutdown*.
            self.result = ShutdownResult(NOT_ATTEMPTED, 0, "no session log; nothing was opened")
            self._events.append({"event": "device_shutdown", **self.result.as_dict()})
            return False
        self.result = shutdown_device(capture_log=log, send_transport_stop=self._stop,
                                      drain_timeout_s=self._timeout, is_connected=self._is_connected)
        self._events.append({"event": "device_shutdown", **self.result.as_dict()})
        if not self.result.ok:
            mark_incomplete(self.result, out_dir=self._out_dir, context=self._context,
                            latch_path=self._latch_path)
        return False  # never swallow an exception; the drain is not error handling
