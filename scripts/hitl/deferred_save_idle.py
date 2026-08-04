"""Wait until the firmware deferred CurrentSet save FSM is idle (serial observable)."""

from __future__ import annotations

import re
import time
from typing import TYPE_CHECKING, Optional

if TYPE_CHECKING:
    from hitl.serial_collector import RunAbort

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


def deferred_save_idle_in_suffix(lines: list[str], *, after_index: int = 0) -> bool:
    """True when no active save work remains in lines[after_index:]."""
    suffix = lines[after_index:]
    if not suffix:
        return True

    last_pending = _last_match_index(suffix, _SAVE_PENDING_RE)
    last_in_progress = _last_match_index(suffix, _SAVE_IN_PROGRESS_RE)
    last_completed = _last_match_index(suffix, _SAVE_COMPLETED_RE)
    last_idle = _last_match_index(suffix, _SAVE_IDLE_RE)
    last_pers_ok = _last_match_index(suffix, _PERS_RESULT_OK_RE)

    active = max(last_pending, last_in_progress)
    settled = max(last_completed, last_idle, last_pers_ok)
    if active > settled:
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


def wait_for_deferred_save_idle(
    collector,
    *,
    after_line_index: int,
    timeout_s: float,
    quiet_s: float = 2.0,
    log_prefix: str = "[deferred-save-idle]",
    abort: Optional[RunAbort] = None,
) -> bool:
    """Return True when deferred save is idle in lines after ``after_line_index``."""
    deadline = time.monotonic() + max(timeout_s, 0.0)
    last_progress_log = 0.0

    while time.monotonic() < deadline:
        if abort is not None and (reason := abort.check()) is not None:
            print(f"{log_prefix} aborted: {reason}")
            return False
        lines = collector.snapshot()
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

    print(f"{log_prefix} timed out after {timeout_s:.0f}s")
    return False
