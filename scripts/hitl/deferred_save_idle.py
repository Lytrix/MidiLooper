"""Wait until the firmware deferred CurrentSet save FSM is idle (serial observable)."""

from __future__ import annotations

import re
import time
from typing import TYPE_CHECKING, Optional

if TYPE_CHECKING:
    from hitl.serial_collector import RunAbort

_PERS_REQUEST_RE = re.compile(r"#CAP,\d+,PERS,request,")
_PERS_WORK_RE = re.compile(r"#CAP,\d+,PERS,work,")
_CLEAR_WAITING_SAVE_RE = re.compile(r"Clear waiting for deferred save", re.I)
_PERSISTENCE_DRAIN_RE = re.compile(r"Persistence drain", re.I)
_PERSISTENCE_DRAIN_STUCK_RE = re.compile(r"Persistence drain stuck", re.I)
_DRAINING_PERSISTENCE_QUEUE_RE = re.compile(
    r"Draining persistence work queue", re.I
)
_SAVE_STATE_SUCCESS_RE = re.compile(r"State saved successfully", re.I)
_CURRENT_SET_SAVED_RE = re.compile(r"CurrentSet saved successfully", re.I)
_PERS_RESULT_OK_RE = re.compile(r"#CAP,\d+,PERS,result,\d+,\d+,\d+,ok\b")
_PERS_SLICE_RE = re.compile(r"#CAP,\d+,PERS,slice,")
_PERS_DISPATCH_RE = re.compile(r"#CAP,\d+,PERS,dispatch,")
_SAVE_IDLE_RE = re.compile(r"#CAP,\d+,SAVE,idle,\d+\b")
_SAVE_PENDING_RE = re.compile(r"#CAP,\d+,SAVE,pending,")
_SAVE_IN_PROGRESS_RE = re.compile(r"#CAP,\d+,SAVE,in_progress,")
_SAVE_COMPLETED_RE = re.compile(r"#CAP,\d+,SAVE,completed,")


def _last_match_index(lines: list[str], pattern: re.Pattern[str]) -> int:
    last = -1
    for index, line in enumerate(lines):
        if pattern.search(line):
            last = index
    return last


def persistence_drain_stuck_in_suffix(lines: list[str], *, after_index: int = 0) -> bool:
    """True when firmware logged a stuck synchronous persistence drain."""
    suffix = lines[after_index:]
    return any(_PERSISTENCE_DRAIN_STUCK_RE.search(line) for line in suffix)


def deferred_save_active_in_suffix(lines: list[str], *, after_index: int = 0) -> bool:
    """True when serial shows deferred save work still in flight after ``after_index``."""
    suffix = lines[after_index:]
    if not suffix:
        return False

    if persistence_drain_stuck_in_suffix(suffix):
        return True

    last_pending = _last_match_index(suffix, _SAVE_PENDING_RE)
    last_in_progress = _last_match_index(suffix, _SAVE_IN_PROGRESS_RE)
    last_completed = _last_match_index(suffix, _SAVE_COMPLETED_RE)
    last_idle = _last_match_index(suffix, _SAVE_IDLE_RE)
    last_pers_ok = _last_match_index(suffix, _PERS_RESULT_OK_RE)
    last_pers_request = _last_match_index(suffix, _PERS_REQUEST_RE)
    last_pers_work = _last_match_index(suffix, _PERS_WORK_RE)
    last_save_success = _last_match_index(suffix, _SAVE_STATE_SUCCESS_RE)
    last_current_set_saved = _last_match_index(suffix, _CURRENT_SET_SAVED_RE)
    last_draining_queue = _last_match_index(suffix, _DRAINING_PERSISTENCE_QUEUE_RE)

    settled = max(
        last_completed,
        last_idle,
        last_pers_ok,
        last_save_success,
        last_current_set_saved,
    )

    active = max(last_pending, last_in_progress)
    if active >= 0:
        if active > settled:
            return True
        after_active = suffix[active + 1 :]
        if any(
            _PERS_SLICE_RE.search(line) or _PERS_DISPATCH_RE.search(line)
            for line in after_active
        ):
            return True

    if last_pers_work >= 0 and last_pers_work > settled:
        return True

    if last_pers_request >= 0 and last_pers_request > settled:
        return True

    if last_draining_queue >= 0 and last_draining_queue > settled:
        return True

    for index in range(len(suffix) - 1, max(-1, len(suffix) - 13), -1):
        line = suffix[index]
        if not (
            _CLEAR_WAITING_SAVE_RE.search(line) or _PERSISTENCE_DRAIN_RE.search(line)
        ):
            continue
        after = suffix[index + 1 :]
        if not any(
            _SAVE_COMPLETED_RE.search(entry)
            or _SAVE_IDLE_RE.search(entry)
            or _PERS_RESULT_OK_RE.search(entry)
            for entry in after
        ):
            return True
        break

    return False


def deferred_save_idle_in_suffix(lines: list[str], *, after_index: int = 0) -> bool:
    """True when no active save work remains in lines[after_index:]."""
    if deferred_save_active_in_suffix(lines, after_index=after_index):
        return False

    suffix = lines[after_index:]
    if not suffix:
        return True

    last_pending = _last_match_index(suffix, _SAVE_PENDING_RE)
    last_in_progress = _last_match_index(suffix, _SAVE_IN_PROGRESS_RE)
    last_completed = _last_match_index(suffix, _SAVE_COMPLETED_RE)
    last_idle = _last_match_index(suffix, _SAVE_IDLE_RE)
    last_pers_ok = _last_match_index(suffix, _PERS_RESULT_OK_RE)
    last_pers_work = _last_match_index(suffix, _PERS_WORK_RE)
    last_save_success = _last_match_index(suffix, _SAVE_STATE_SUCCESS_RE)
    last_current_set_saved = _last_match_index(suffix, _CURRENT_SET_SAVED_RE)
    last_draining_queue = _last_match_index(suffix, _DRAINING_PERSISTENCE_QUEUE_RE)

    settled = max(
        last_completed,
        last_idle,
        last_pers_ok,
        last_save_success,
        last_current_set_saved,
    )

    active = max(last_pending, last_in_progress)
    if active > settled:
        return False
    if last_pers_work > settled:
        return False
    if last_draining_queue > settled:
        return False
    if settled < 0:
        return True

    after_settled = suffix[settled + 1 :]
    if any(
        _PERS_SLICE_RE.search(line) or _PERS_DISPATCH_RE.search(line)
        for line in after_settled
    ):
        return False
    return True


def deferred_save_idle_in_tail(lines: list[str], *, lookback: int = 80) -> bool:
    """True when recent serial tail shows deferred save is idle."""
    tail = lines[-lookback:] if lookback > 0 else lines
    offset = max(0, len(lines) - len(tail))
    return deferred_save_idle_in_suffix(lines, after_index=offset)


def deferred_save_active_in_tail(lines: list[str], *, lookback: int = 80) -> bool:
    """True when recent serial tail shows deferred save still in flight."""
    if not lines:
        return False
    offset = max(0, len(lines) - lookback)
    return deferred_save_active_in_suffix(lines, after_index=offset)


def wait_for_deferred_save_idle(
    collector,
    *,
    after_line_index: int,
    timeout_s: float,
    quiet_s: float = 2.0,
    log_prefix: str = "[deferred-save-idle]",
    abort: Optional[RunAbort] = None,
) -> bool:
    """Return True when deferred save is idle in lines after ``after_line_index``.

    When ``timeout_s`` is 0, wait until idle or ``abort`` fires (no wall-clock cap).
    """
    deadline: Optional[float] = None
    if timeout_s > 0:
        deadline = time.monotonic() + timeout_s
    last_progress_log = 0.0

    while deadline is None or time.monotonic() < deadline:
        if abort is not None and (reason := abort.check()) is not None:
            print(f"{log_prefix} aborted: {reason}")
            return False
        lines = collector.snapshot()
        if persistence_drain_stuck_in_suffix(lines, after_index=after_line_index):
            print(
                f"{log_prefix} firmware persistence drain stuck — "
                "stop transport and wait for async save before clear, or reboot device"
            )
            return False
        if deferred_save_idle_in_suffix(lines, after_index=after_line_index):
            print(f"{log_prefix} idle (suffix after line {after_line_index})")
            return True

        now = time.monotonic()
        if now - last_progress_log >= 5.0:
            tail = lines[-3:] if lines else []
            print(
                f"{log_prefix} waiting for save idle "
                f"lines={len(lines)} tail={tail[-1] if tail else 'none'}"
            )
            last_progress_log = now

        time.sleep(0.05)

    if deadline is not None:
        print(f"{log_prefix} timed out after {timeout_s:.0f}s")
    return False


def wait_for_deferred_save_idle_if_active(
    collector,
    *,
    after_line_index: int,
    log_prefix: str = "[deferred-save-idle]",
    abort: Optional[RunAbort] = None,
    max_timeout_s: float = 0.0,
) -> bool:
    """Poll until save idle only when serial shows active save work; otherwise return immediately."""
    lines = collector.snapshot()
    if not deferred_save_active_in_suffix(lines, after_index=after_line_index):
        return True
    print(f"{log_prefix} deferred save active — waiting for idle")
    return wait_for_deferred_save_idle(
        collector,
        after_line_index=after_line_index,
        timeout_s=max_timeout_s,
        log_prefix=log_prefix,
        abort=abort,
    )
