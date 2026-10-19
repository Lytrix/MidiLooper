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

Before the first control-surface gesture, the host waits for the boot banner
``DIAG,boot_reset``, the timestamped ``DIAG,loop_rem,boot_commit`` row, and a
timestamp-qualified ``DFRAME`` emitted after that commit. The human-readable
``DisplayManager: Boot setup complete`` line remains diagnostic, not a required gate row:
it is absent from retained runs even when both the commit and later app-paint frames are present.

This substitution is grounded in the production path, not elapsed time. ``main`` emits
``boot_commit`` in the same branch that calls ``DisplayManager::finishBootSetup``. That sets
``bootSetupComplete_``; ``DisplayManager::update`` returns while the boot screen is visible
until setup is complete and the title hold has elapsed, then resumes paint/transfer. Its
existing ``DFRAME`` therefore witnesses the app-paint path after that handoff. It does not
prove physical pixels or completed DMA. Existing rows have no boot-generation identity, so
timestamp qualification cannot distinguish an older boot's delayed row whose timestamp
happens to fit the current boot's interval.

``DFRAME`` is not Tier-A and may be shed. Missing or malformed commit/frame evidence therefore
fails closed; elapsed device time, including the formerly accepted 2500 ms after
``boot_commit``, cannot prove readiness. A fresh boot without a qualified frame times out
without gestures. No firmware marker is added.

It deliberately does NOT wait for ``DIAG,post_restore_census``: the recorded setup-latency
evidence ``captures/s4f22-base-device-16`` (see the docstring of
``scripts/test_hitl_boot_settle.py``) shows the restore load is never the readiness gate and the
settle is never a fixed sleep -- that managed run spent its whole setup budget on the boot-time
deferred save and never recorded (``reason: deferred_save_not_idle``) while the device was
answering host-driven interaction ~130 s before its own ``post_restore_census`` marker.

The gate is row evidence, not a fixed sleep: it reads the collector's captured lines only and
never writes to the device.
"""

from __future__ import annotations

import re
import time
from typing import Protocol, Sequence

#: The boot banner the device prints at the top of every boot.
BOOT_BANNER_MARKER = "DIAG,boot_reset"
#: Human-readable debug line emitted by finishBootSetup; diagnostic only because it can be lost.
BOOT_SETUP_COMPLETE_ROW = "DisplayManager: Boot setup complete"
#: Boot/loop-owner handoff row: the loop-state commit of the boot finished.
BOOT_COMMIT_ROW = "DIAG,loop_rem,boot_commit"
#: The timestamped owner commit is required; DFRAME separately proves display progression.
HANDOFF_ROWS = (BOOT_COMMIT_ROW,)
#: App-paint-path witness, not proof of physical pixels, submission success or DMA completion.
#: `DISP` is deliberately NOT a witness: stop and transport snapshot emitters also use that tag.
APP_SCREEN_WITNESS_ROW = "DFRAME"
#: The firmware's measured boot-title extra hold (`kBootScreenExtraHoldMs`,
#: `src/DisplayManager/BootDisplay.cpp`).
BOOT_SCREEN_EXTRA_HOLD_MS = 2000
#: Historical timer threshold, retained for compatibility/regression fixtures only.
#: Elapsed time at this threshold NEVER qualifies boot readiness.
APP_SCREEN_RELEASE_HOLD_MS = 2500


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

#: A device-timestamped capture row: `#CAP,<micros>,...`.
_DEVICE_STAMP_RE = re.compile(r"^#CAP,([0-9]+),")
#: Existing DFRAME schema: timestamp followed by six unsigned numeric fields.
_APP_SCREEN_WITNESS_RE = re.compile(rf"#CAP,([0-9]+),{APP_SCREEN_WITNESS_ROW}(?:,[0-9]+){{6}}")


def _device_us(line: str) -> int | None:
    """The row's device microseconds from its ``#CAP,<us>,`` prefix, or None on a row without one."""
    match = _DEVICE_STAMP_RE.match(line)
    return int(match.group(1)) if match is not None else None


def boot_handoff_complete(lines: Sequence[str]) -> bool:
    """True when a fresh boot's owner commit is followed by its app-paint-path witness.

    The last banner opens the current window. A timestamped commit and a well-formed DFRAME
    after it are required. DisplayManager emits DFRAME only after its boot-screen guard allows
    application painting; timer expiry or the human-readable setup line alone never qualifies.
    This does not attest physical pixels or completed DMA; see the module's evidence limits.
    """
    start = _last_banner_index(lines)
    if start is None:
        return False
    window = lines[start:]
    commit_index = next(
        (index for index, line in enumerate(window) if BOOT_COMMIT_ROW in line), None
    )
    if commit_index is None:
        return False
    commit_us = _device_us(window[commit_index])
    banner_us = _device_us(window[0])
    if commit_us is None or (banner_us is not None and commit_us < banner_us):
        return False
    for line in window[commit_index + 1 :]:
        witness = _APP_SCREEN_WITNESS_RE.fullmatch(line.rstrip("\r\n"))
        if witness is None:
            continue
        witness_us = int(witness.group(1))
        if witness_us > commit_us and (banner_us is None or witness_us >= banner_us):
            return True
    return False


def wait_for_boot_handoff(
    collector: _LineSource, *, timeout_s: float = 30.0, poll_s: float = 0.05
) -> str:
    """Wait for the device's boot handoff before any control-surface gesture.

    Reads the collector's lines only; the device is never written to. Returns exactly one of:

    * ``"not-fresh-boot"`` -- no boot banner in the window at call time: the run attached to an
      already-booted device, so there is nothing to wait for and the run is not delayed;
    * ``"complete"`` -- the timestamped boot commit and a later app-paint-path witness are present
      are present in the window opened by the last banner;
    * ``"timeout"`` -- a fresh banner is present but the evidence is incomplete within
      ``timeout_s`` (the caller refuses gestures). The default bound remains 30.0 s;
      shed witness rows fail closed rather than being replaced by a timer.
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
