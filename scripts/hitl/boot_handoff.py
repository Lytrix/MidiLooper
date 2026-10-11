"""Gate the host leg's control gestures on the device's own boot handoff rows.

Owner panel report, 2026-10-12: "something in the test is starting during bootup so too soon
which makes the display hang after boot". Three captures pin the timing:

* ``captures/i3a-edge-effect-boot-rerun-t8s4-03/session_20261012_002337.log`` -- the first host
  control press (Ch16 Note67, "Track 8") lands at device micros 7,374,166, while
  ``DisplayManager: Boot setup complete`` arrives ~17 s in and the row
  ``#CAP,17838519,DIAG,loop_rem,boot_commit,...`` only at 17,838,519 micros: the leg pressed
  ~10 s before the boot handoff finished.
* ``captures/i3a-edge-effect-boot-t8s4-01/session_20261011_235221.log`` -- the same shape (first
  press 7,406,543 micros, ``DisplayManager: Boot setup complete`` ~15 s in, ``boot_commit`` at
  17,521,199 micros).
* ``captures/i3a-edge-effect-boot-rerun-t8s4-02/session_20261012_002039.log`` -- the leg's
  LOOP_EDIT proof missed the boot's own ack and pressed the edit button during the unfinished
  boot; the device read the press as 1 ms (``[BTN] Short press detected: duration=1 <
  effectiveLongPress=600``) and cycled LOOP_EDIT -> NOTE_EDIT, so the record arm landed in
  NOTE_EDIT and aborted (``recording not confirmed before note stream``).

This module is the host leg's start sequencing fix: before the first control-surface gesture the
run waits for the device's own short display/boot handoff rows -- the boot banner
``DIAG,boot_reset`` plus both ``DisplayManager: Boot setup complete`` and
``DIAG,loop_rem,boot_commit`` -- and refuses to gesture into a boot that has not finished.

It deliberately does NOT wait for ``DIAG,post_restore_census``: the recorded setup-latency
evidence ``captures/s4f22-base-device-16`` (see the docstring of
``scripts/test_hitl_boot_settle.py``) shows the restore load is never the readiness gate and the
settle is never a fixed sleep -- that managed run spent its whole setup budget on the boot-time
deferred save and never recorded (``reason: deferred_save_not_idle``) while the device was
answering host-driven interaction ~130 s before its own ``post_restore_census`` marker.

The gate is **row evidence, not a sleep**: it reads the collector's captured lines only and
never writes to the device.
"""

from __future__ import annotations

import time
from typing import Protocol, Sequence

#: The boot banner the device prints at the top of every boot.
BOOT_BANNER_MARKER = "DIAG,boot_reset"
#: Display-side handoff row: the panel's post-boot setup is complete.
BOOT_SETUP_COMPLETE_ROW = "DisplayManager: Boot setup complete"
#: Boot/loop-owner handoff row: the loop-state commit of the boot finished.
BOOT_COMMIT_ROW = "DIAG,loop_rem,boot_commit"
#: Both rows are required; either one alone is not the handoff.
HANDOFF_ROWS = (BOOT_SETUP_COMPLETE_ROW, BOOT_COMMIT_ROW)


class _LineSource(Protocol):
    """The part of the serial collectors this gate reads (and nothing more)."""

    def snapshot(self) -> list[str]: ...


def _last_banner_index(lines: Sequence[str]) -> int | None:
    """Index of the last boot banner in ``lines``, or None when the window holds none."""
    for index in range(len(lines) - 1, -1, -1):
        if BOOT_BANNER_MARKER in lines[index]:
            return index
    return None


def fresh_boot_in_window(lines: Sequence[str]) -> bool:
    """True when the examined window holds the device's own boot banner."""
    return _last_banner_index(lines) is not None


def boot_handoff_complete(lines: Sequence[str]) -> bool:
    """True when both handoff rows appear in the window opened by the last boot banner.

    The window starts at the *last* banner: a device that booted again restarts its handoff, so
    a banner/handoff pair from an earlier boot must not vouch for the current one.
    """
    start = _last_banner_index(lines)
    if start is None:
        return False
    window = lines[start:]
    return all(any(row in line for line in window) for row in HANDOFF_ROWS)


def wait_for_boot_handoff(
    collector: _LineSource, *, timeout_s: float = 30.0, poll_s: float = 0.05
) -> str:
    """Wait for the device's boot handoff before any control-surface gesture.

    Reads the collector's lines only; the device is never written to. Returns exactly one of:

    * ``"not-fresh-boot"`` -- no boot banner in the window at call time: the run attached to an
      already-booted device, so there is nothing to wait for and the run is not delayed;
    * ``"complete"`` -- both handoff rows are present in the window opened by the last banner;
    * ``"timeout"`` -- a fresh banner is present but the handoff rows do not arrive within
      ``timeout_s`` (the caller refuses the gestures).
    """
    lines = list(collector.snapshot())
    if not fresh_boot_in_window(lines):
        return "not-fresh-boot"
    if boot_handoff_complete(lines):
        return "complete"
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        time.sleep(max(poll_s, 0.0))
        if boot_handoff_complete(list(collector.snapshot())):
            return "complete"
    return "timeout"
