"""Wait until the firmware deferred CurrentSet save FSM is idle (serial observable)."""

from __future__ import annotations

import re
import time

_PERS_RESULT_OK_RE = re.compile(r"#CAP,\d+,PERS,result,\d+,\d+,\d+,ok\b")
_PERS_SLICE_RE = re.compile(r"#CAP,\d+,PERS,slice,")
_PERS_DISPATCH_RE = re.compile(r"#CAP,\d+,PERS,dispatch,")
_SAVE_IDLE_RE = re.compile(r"#CAP,\d+,SAVE,idle,\d+\b")
_PERS_RESULT_OK_RE_TAIL = _PERS_RESULT_OK_RE


def deferred_save_idle_in_tail(lines: list[str], *, lookback: int = 80) -> bool:
    """True when recent serial tail shows SAVE idle after PERS,result,ok with no newer slices."""
    tail = lines[-lookback:] if lookback > 0 else lines
    last_ok_index = -1
    for index, line in enumerate(tail):
        if _PERS_RESULT_OK_RE_TAIL.search(line):
            last_ok_index = index
        if _PERS_SLICE_RE.search(line) or _PERS_DISPATCH_RE.search(line):
            if last_ok_index >= 0 and index > last_ok_index:
                last_ok_index = -1
    if last_ok_index < 0:
        return False
    after_ok = tail[last_ok_index + 1 :]
    if any(_PERS_SLICE_RE.search(line) or _PERS_DISPATCH_RE.search(line) for line in after_ok):
        return False
    return any(_SAVE_IDLE_RE.search(line) for line in after_ok)


def wait_for_deferred_save_idle(
    collector,
    *,
    after_line_index: int,
    timeout_s: float,
    quiet_s: float = 2.0,
    log_prefix: str = "[deferred-save-idle]",
) -> bool:
    """Return True when a PERS,result,ok appeared and no PERS,slice followed for quiet_s."""
    deadline = time.monotonic() + max(timeout_s, 0.0)
    last_ok_index = -1
    last_progress_log = 0.0

    while time.monotonic() < deadline:
        lines = collector.snapshot()
        for index in range(max(0, after_line_index), len(lines)):
            line = lines[index]
            if _PERS_RESULT_OK_RE.search(line):
                last_ok_index = index
            if _PERS_SLICE_RE.search(line) or _PERS_DISPATCH_RE.search(line):
                if last_ok_index >= 0 and index > last_ok_index:
                    last_ok_index = -1

        now = time.monotonic()
        if now - last_progress_log >= 5.0:
            tail = lines[-3:] if lines else []
            print(
                f"{log_prefix} waiting ok_index={last_ok_index} "
                f"lines={len(lines)} tail={tail[-1] if tail else 'none'}"
            )
            last_progress_log = now

        if last_ok_index >= 0:
            quiet_deadline = time.monotonic() + quiet_s
            while time.monotonic() < quiet_deadline:
                newer = collector.snapshot()
                if any(
                    _PERS_SLICE_RE.search(line) or _PERS_DISPATCH_RE.search(line)
                    for line in newer[last_ok_index + 1 :]
                ):
                    last_ok_index = -1
                    break
                time.sleep(0.05)
            else:
                print(f"{log_prefix} idle after PERS,result,ok (line {last_ok_index})")
                return True

        time.sleep(0.05)

    print(f"{log_prefix} timed out after {timeout_s:.0f}s")
    return False
