"""Bounded cancellation for a HITL run (task 3.2 of `response-driven-hitl-acceptance`).

A managed run is bounded by a wall-clock watchdog (`managed_capture`), but the *scenario* that overruns it used
to keep going: the watchdog returned, the capture child was released, and the scenario was still on the wire.
Abandoning a run has to hold two properties, and this module is where each one has a single owner:

* **Nothing is pressed after the abort.** Every test edge goes through one `GuardedMidiOut`, which refuses to
  send once the run is cancelled, so no helper can "just send one more" gesture. The refusal is an exception
  (`RunCancelled`), so the scenario *leaves* rather than continuing to compute on a cancelled run, and
  `cancellable_sleep` bounds the time it can spend between two sends.
* **The notes the run was holding are released.** A run stopped between a `note_on` and its `note_off` leaves
  the device sounding a note; the guard remembers what it put down and releases it -- one `note_off` per held
  note, plus one channel-mode `All Notes Off` (CC 123) per channel the run touched, so a note it lost track of
  is cleared too. A stuck note is not a test gesture, it is damage to the device's musical state.

**The teardown is not a test gesture.** Stopping the transport and draining persistence is exactly what must
still happen when a run is abandoned, so it runs in an explicitly permitted phase
(`GuardedMidiOut.teardown_call`), and its edges are counted separately. That separation is what lets a report
say that *nothing but teardown* went out after the cancel.

Cancellation is cooperative and observable: `CancelScope.check()` raises `RunCancelled` at an action boundary,
`CorrelatedGesture.wait_for(cancel=…)` stops waiting, and the scope of the run in progress is registered
(`active_cancel_scope`) because the watchdog that cancels lives in `managed_capture` while the driver owns the
scenario.
"""

from __future__ import annotations

import contextlib
import threading
import time
from dataclasses import dataclass, field
from typing import Callable, Iterator, Optional

import mido

#: How long a canceller waits for a notified scenario to leave the wire before it gives up (see
#: `managed_capture._run_with_watchdog`). Bounded on purpose: an abandoned run must not hold the capture.
DEFAULT_CANCEL_GRACE_S = 30.0

#: Longest slice a `cancellable_sleep` spends between two checks of the scope.
_SLEEP_SLICE_S = 0.05

#: A remainder this small is the end of the sleep, not another slice: summing floating-point slices leaves a
#: sliver behind, and sleeping it would add a degenerate iteration.
_EPS_S = 1e-6

#: Channel-mode messages that clear a channel: `All Sound Off` (120) and `All Notes Off` (123).
_SILENCE_CONTROLLERS = (120, 123)


class RunCancelled(Exception):
    """Raised at an action boundary once the run has been cancelled.

    The message is the *reason* the run was cancelled, so the failure reads as "watchdog 360.0s" rather than as
    a bare exception.
    """

    def __init__(self, reason: str) -> None:
        super().__init__(reason)
        self.reason = reason


@dataclass
class CancelScope:
    """Whether the run in progress has been cancelled, and why.

    The first cancellation wins: a watchdog and a caught `KeyboardInterrupt` racing must not leave two reasons,
    and a later call cannot un-cancel or rewrite the cause.
    """

    name: str = "run"
    _reason: Optional[str] = None
    _event: threading.Event = field(default_factory=threading.Event)
    _lock: threading.Lock = field(default_factory=threading.Lock)

    def cancel(self, reason: str) -> bool:
        """Cancel the run. Returns True when *this* call is the one that cancelled it."""
        with self._lock:
            if self._reason is not None:
                return False
            self._reason = reason
            self._event.set()
            return True

    @property
    def cancelled(self) -> bool:
        return self._event.is_set()

    @property
    def reason(self) -> Optional[str]:
        return self._reason

    def check(self) -> None:
        """Raise `RunCancelled` when the run has been cancelled; the action-boundary check."""
        if self._event.is_set():
            raise RunCancelled(self._reason or "cancelled")


@dataclass
class CancellationRecord:
    """What a run's own context observed: whether it was cancelled, why, and what the guard did."""

    cancelled: bool = False
    reason: Optional[str] = None
    released_notes: int = 0
    record: dict = field(default_factory=dict)

    def as_dict(self) -> dict:
        """The run's own record, flat, for the run report."""
        return {"cancelled": self.cancelled, "reason": self.reason,
                "released_notes": self.released_notes, **self.record}


@dataclass
class _Counters:
    """What went out, and what was refused, so a report can state it."""

    edges: int = 0
    teardown_edges: int = 0
    release_edges: int = 0
    refused: int = 0
    notes_on: int = 0
    notes_off: int = 0


class GuardedMidiOut:
    """The one place a run puts MIDI on the wire.

    Drop-in for `mido`'s output (it has `send` and `close`), so the press helpers and the note streams need no
    knowledge of it. Once the scope is cancelled it refuses every send that is not part of the teardown phase,
    counts the refusal, and raises `RunCancelled`.
    """

    def __init__(self, port, *, scope: Optional[CancelScope] = None, name: str = "") -> None:
        self._port = port
        self.scope = scope if scope is not None else CancelScope(name=name or "run")
        self.name = name or self.scope.name
        self._lock = threading.Lock()
        self._held: set[tuple[int, int]] = set()
        self._channels: set[int] = set()
        self._teardown_depth = 0
        self.counters = _Counters()

    # -- output ----------------------------------------------------------------------------------------
    def send(self, message) -> None:
        """Send `message`, or refuse it because this run has been cancelled."""
        with self._lock:
            in_teardown = self._teardown_depth > 0
        if not in_teardown and self.scope.cancelled:
            with self._lock:
                self.counters.refused += 1
            raise RunCancelled(self.scope.reason or "cancelled")
        self._port.send(message)
        self._track(message, teardown=in_teardown)

    def close(self) -> None:
        """Close the underlying port, tolerating a port that is already gone."""
        close = getattr(self._port, "close", None)
        if close is not None:
            close()

    # -- teardown phase --------------------------------------------------------------------------------
    @contextlib.contextmanager
    def teardown(self) -> Iterator["GuardedMidiOut"]:
        """Permit sends for the teardown phase, counted apart from the run's own gestures."""
        with self._lock:
            self._teardown_depth += 1
        try:
            yield self
        finally:
            with self._lock:
                self._teardown_depth -= 1

    def teardown_call(self, fn: Callable[[], object]) -> object:
        """Run `fn` in a teardown phase. This is what a shutdown's transport stop goes through."""
        with self.teardown():
            return fn()

    @contextlib.contextmanager
    def run(self) -> Iterator["CancellationRecord"]:
        """Own a run: absorb `RunCancelled`, release held notes, and unregister the scope -- in that order.

        Used as the **outer** context of a scenario's body, with the driver's shutdown as an inner one, so the
        body needs no re-indentation and no exit path is forgotten: the teardown still runs (it is inner, and
        `teardown_call` lets its transport stop out), this one then releases whatever the run was holding and
        unregisters the run, and the caller leaves with a reason to report. Only `RunCancelled` is absorbed;
        any other exception is the caller's.
        """
        record = CancellationRecord()
        try:
            yield record
        except RunCancelled as exc:
            record.cancelled = True
            record.reason = exc.reason
        finally:
            record.released_notes = self.release_held_notes()
            record.record = self.as_dict()
            end_run(self.scope)

    def in_teardown(self) -> bool:
        with self._lock:
            return self._teardown_depth > 0

    # -- held notes ------------------------------------------------------------------------------------
    @property
    def held_notes(self) -> tuple[tuple[int, int], ...]:
        """The `(channel, note)` pairs this guard believes are still down."""
        with self._lock:
            return tuple(sorted(self._held))

    def release_held_notes(self) -> int:
        """Release everything this guard is holding; the number of messages sent.

        Never raises: the abort path must not fail on the thing that makes the device safe. The per-note
        releases bypass the cancel refusal (they are the release, not a new gesture) and clear the tracking, so
        a second call releases nothing.
        """
        with self._lock:
            held = sorted(self._held)
            channels = sorted(self._channels)
            self._held.clear()
        count = 0
        for channel, note in held:
            if self._safe_send(mido.Message("note_off", channel=channel, note=note, velocity=0)):
                count += 1
        for channel in channels:
            for control in _SILENCE_CONTROLLERS:
                if self._safe_send(mido.Message("control_change", channel=channel, control=control, value=0)):
                    count += 1
        with self._lock:
            # The tracking is cleared with the sends, so a second call releases nothing: the release is
            # idempotent, and a later send registers its channel again.
            self._channels.clear()
            self.counters.release_edges += count
        return count

    # -- reporting -------------------------------------------------------------------------------------
    def as_dict(self) -> dict:
        """This guard's own record, for the run report."""
        with self._lock:
            counters = _Counters(**vars(self.counters))
            held = tuple(sorted(self._held))
        return {
            "name": self.name,
            "cancelled": self.scope.cancelled,
            "cancel_reason": self.scope.reason,
            "edges": counters.edges,
            "teardown_edges": counters.teardown_edges,
            "release_edges": counters.release_edges,
            "refused_after_cancel": counters.refused,
            "notes_on": counters.notes_on,
            "notes_off": counters.notes_off,
            "held_notes": [list(pair) for pair in held],
        }

    # -- internals -------------------------------------------------------------------------------------
    def _safe_send(self, message) -> bool:
        try:
            self._port.send(message)
        except Exception:  # pragma: no cover - a dying port must not break the release
            return False
        return True

    def _track(self, message, *, teardown: bool) -> None:
        with self._lock:
            if teardown:
                self.counters.teardown_edges += 1
            else:
                self.counters.edges += 1
            kind = getattr(message, "type", "")
            channel = getattr(message, "channel", None)
            note = getattr(message, "note", None)
            velocity = getattr(message, "velocity", 0)
            if channel is not None:
                self._channels.add(int(channel))
            if kind == "note_on" and note is not None and velocity > 0:
                self._held.add((int(channel), int(note)))
                self.counters.notes_on += 1
            elif kind == "note_on" and note is not None:
                self._held.discard((int(channel), int(note)))
            if kind == "note_off" and note is not None:
                self._held.discard((int(channel), int(note)))
                self.counters.notes_off += 1
            if (kind == "control_change" and channel is not None
                    and getattr(message, "control", None) in _SILENCE_CONTROLLERS
                    and getattr(message, "value", 0) == 0):
                self._held = {pair for pair in self._held if pair[0] != int(channel)}


# -- the run in progress ---------------------------------------------------------------------------------


@dataclass
class _Registry:
    lock: threading.Lock = field(default_factory=threading.Lock)
    scope: Optional[CancelScope] = None


_ACTIVE = _Registry()


def begin_run(*, scope: Optional[CancelScope] = None, name: str = "run") -> CancelScope:
    """Register the scope of the run in progress and return it.

    One run per process: a managed run owns a capture child, a serial port and a MIDI port, so a second
    registration without the first ending is a programming error and is refused loudly rather than silently
    replacing the scope a watchdog would cancel.
    """
    with _ACTIVE.lock:
        if _ACTIVE.scope is not None and not _ACTIVE.scope.cancelled:
            raise RuntimeError(
                f"another HITL run is already active ({_ACTIVE.scope.name!r}); "
                "end it before starting a new one")
        _ACTIVE.scope = scope if scope is not None else CancelScope(name=name)
        return _ACTIVE.scope


def end_run(scope: Optional[CancelScope] = None) -> None:
    """Unregister `scope` (or whatever is registered) so the next run can begin."""
    with _ACTIVE.lock:
        if scope is None or _ACTIVE.scope is scope:
            _ACTIVE.scope = None


def active_cancel_scope() -> Optional[CancelScope]:
    """The scope of the run in progress, or None when no run is registered."""
    with _ACTIVE.lock:
        return _ACTIVE.scope


def cancel_active_run(reason: str) -> bool:
    """Cancel the run in progress. False when no run is registered or it was already cancelled."""
    scope = active_cancel_scope()
    if scope is None:
        return False
    return scope.cancel(reason)


def cancellable_sleep(seconds: float, *, scope: Optional[CancelScope] = None,
                      sleep: Callable[[float], None] = time.sleep,
                      now: Callable[[], float] = time.monotonic) -> None:
    """Sleep `seconds`, in slices, raising `RunCancelled` as soon as the run is cancelled.

    A note stream is the run's longest stretch between two sends, so this is where a cancel becomes prompt:
    without it an abandoned run would spend the rest of its take asleep while the capture is being released.
    The clock and the sleep are injectable so the behaviour is testable without real time.
    """
    target = max(seconds, 0.0)
    if target == 0.0:
        if scope is not None:
            scope.check()
        return
    deadline = now() + target
    while True:
        if scope is not None:
            scope.check()
        remaining = deadline - now()
        if remaining <= _EPS_S:
            return
        sleep(min(_SLEEP_SLICE_S, remaining))
