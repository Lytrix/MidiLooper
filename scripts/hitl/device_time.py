#!/usr/bin/env python3
"""The device's own clock, as carried by serial rows — and what it says about a row's freshness.

**Why this exists (`captures/s4f22-step11-h2`).** Capture telemetry is written through the firmware's
fixed evidence ring, and the ring flushes its records in batches. When the reader has fallen behind,
a batch of **older** records lands in the log *after* newer rows, so the file's line order is not the
device's order. Measured on that run: `BAR` rows stamped 850-1864 s interleaved after a 1855 s
`state=STOPPED` reply, and 343 `BAR` rows plus 10 453 `DIAG` rows arriving out of device order.

An index window (`lines[after_index:]`) therefore cannot answer "is the transport running now?" — it
saw the drained `BAR` rows as current, the run skipped its transport start and then pressed a "stop"
onto an already-stopped device, which *started* it, and the clear could not drain.

Every `#CAP,<us>,…` (and `#<FAMILY>,<us>,…`) row carries the device's `micros()`. A row whose stamp is
older than the newest stamp already seen was emitted before that newer row: it is drained history, not
news. Rows without a stamp are written straight to `Serial` by the firmware (for example
`#STOP_STATUS,us=…`, `[StorageManager] …`, `[1857.073] [INFO] Transport started`) and are live by
construction, so they are never dropped.

A reboot restarts `micros()` at zero, so a boot row resets the watermark instead of being compared
with the pre-boot clock.
"""

from __future__ import annotations

import re
from typing import Optional, Sequence

#: `#CAP,<us>,…` and `#DMA_XFER,<us>,…`: a family tag followed by the device timestamp.
_DEVICE_STAMP_RE = re.compile(r"^#(?:CAP|[A-Z][A-Z0-9_]*),(\d+),")

#: Rows that mark a device restart (`micros()` begins again). `BOOT,load,start` and the banner are
#: emitted within milliseconds of the reset, so seeing one with a stamp below the watermark means a
#: reboot rather than a drained record.
_BOOT_RESTART_MARKERS = (
    "DIAG,boot_reset,",
    "BOOT,load,start",
    "BOOT,usb_host,",
    "BOOT,ram1,",
)


def device_stamp_us(line: str) -> Optional[int]:
    """The device `micros()` stamp a serial row carries, or None when it carries none."""
    match = _DEVICE_STAMP_RE.match(line)
    if match is None:
        return None
    return int(match.group(1))


def is_boot_restart_row(line: str) -> bool:
    """True for a row that can only come from a device that has just restarted."""
    return any(marker in line for marker in _BOOT_RESTART_MARKERS)


def newest_device_us(lines: Sequence[str], *, upto_index: Optional[int] = None) -> Optional[int]:
    """The newest device stamp in ``lines[:upto_index]`` — the device's clock at that point."""
    newest: Optional[int] = None
    window = lines if upto_index is None else lines[: max(upto_index, 0)]
    for line in window:
        stamp = device_stamp_us(line)
        if stamp is None:
            continue
        if newest is None or stamp > newest:
            newest = stamp
    return newest


def device_clock_step(line: str, watermark_us: Optional[int]) -> Optional[int]:
    """Advance the watermark by one row, without judging the row.

    A boot row restarts the device clock, so it lowers the watermark; any other stamped row raises it;
    an unstamped row leaves it alone.
    """
    stamp = device_stamp_us(line)
    if stamp is None:
        return watermark_us
    if is_boot_restart_row(line) and (watermark_us is None or stamp < watermark_us):
        return stamp
    if watermark_us is None or stamp > watermark_us:
        return stamp
    return watermark_us


def is_drained_history(line: str, *, watermark_us: Optional[int]) -> bool:
    """True when a stamped row predates the watermark — a drained record, not current news.

    ``watermark_us`` is the device clock *before* this row (see `filter_live_lines`), so a row that
    arrives after a newer one is recognised whether or not the run's baseline is known.
    """
    stamp = device_stamp_us(line)
    if stamp is None or watermark_us is None:
        return False
    if is_boot_restart_row(line):
        return False
    return stamp < watermark_us


def filter_live_lines(lines: Sequence[str], *, after_index: int = 0) -> list[str]:
    """``lines[after_index:]`` minus the rows the device had already superseded.

    The watermark starts at the newest stamp seen **before** ``after_index`` (the run's baseline), so a
    batch the ring was still holding when the run started is drained history even if it lands later in
    the file. Order is preserved, and unstamped (directly printed) rows are always kept.
    """
    start = max(after_index, 0)
    watermark = newest_device_us(lines, upto_index=start)
    live: list[str] = []
    for line in lines[start:]:
        drained = is_drained_history(line, watermark_us=watermark)
        watermark = device_clock_step(line, watermark)
        if not drained:
            live.append(line)
    return live


#: The firmware's logger prefix: `[<seconds>.<milliseconds>] [LEVEL] message`. Unlike `#CAP` rows
#: these lines are written straight to `Serial`, and their prefix is the same device clock.
_HUMAN_STAMP_RE = re.compile(r"^\[(\d+)\.(\d{3})\]\s")


def human_line_device_us(line: str) -> Optional[int]:
    """The device clock a directly printed logger line carries, in microseconds, or None."""
    match = _HUMAN_STAMP_RE.match(line)
    if match is None:
        return None
    return int(match.group(1)) * 1_000_000 + int(match.group(2)) * 1_000


def newest_row_clock_us(lines: Sequence[str], *, upto_index: Optional[int] = None) -> Optional[int]:
    """The newest device clock in ``lines[:upto_index]``, from **either** row family.

    "How far had the device's clock reached?" is the maximum over both encodings: `#CAP,<us>,…` carries
    `micros()` directly and a directly printed logger line (`[<s>.<ms>] [LEVEL] …`) carries the same clock as a
    prefix. `newest_device_us` answers the same question for the ring family alone, which is what the ring
    drain needs; a *gesture's* floor wants both, because the receipts it waits for may be printed either way.
    """
    newest: Optional[int] = None
    window = lines if upto_index is None else lines[: max(upto_index, 0)]
    for line in window:
        stamp = device_stamp_us(line)
        if stamp is None:
            stamp = human_line_device_us(line)
        if stamp is None:
            continue
        if newest is None or stamp > newest:
            newest = stamp
    return newest
