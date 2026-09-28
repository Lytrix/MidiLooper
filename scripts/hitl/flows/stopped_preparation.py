"""Scoped stopped preparation — clear one explicitly authorized slot.

The production clear gesture is a long press on that slot's loop button
(`CLEAR_TRACK_FOR_SLOT`), exactly the gesture the baseline recipe uses. This
module owns only the *scoped* form the accepted-sequence reproducer needs: it
never infers completion from the press or from a sleep, it classifies the
firmware's own clear evidence, and it treats a wrong target, a refusal and an
abort as non-success so the caller stops before mutating anything else.

It is deliberately stopped-only preparation: the caller must have stopped the
transport first. The measured acceptance window stays continuously playing.
"""

from __future__ import annotations

import re
import time
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Callable, Optional

from hitl.actions import slots
from hitl.context import ActionContext

DEFAULT_LONG_PRESS_MS = slots.DEFAULT_LONG_PRESS_MS
DEFAULT_CLEAR_TIMEOUT_S = 20.0

_IGNORED_EMPTY = re.compile(r"Clear ignored .*\(track (\d+) slot (\d+)\)")
_CLEARED_SLOT = re.compile(r"MIDI: Clear selected slot (\d+)")


class ClearStatus(str, Enum):
    """Terminal classification of one scoped clear request."""

    CLEARED = "CLEARED"
    ALREADY_EMPTY = "ALREADY_EMPTY"
    REFUSED = "REFUSED"
    ABORTED = "ABORTED"
    NOT_SELECTED = "NOT_SELECTED"
    WRONG_TARGET = "WRONG_TARGET"
    UNCONFIRMED = "UNCONFIRMED"

    @property
    def ok(self) -> bool:
        return self in (ClearStatus.CLEARED, ClearStatus.ALREADY_EMPTY)


@dataclass(frozen=True)
class ClearOutcome:
    """One scoped preparation result. `slot` is 1-based (the user-facing label)."""

    slot: int
    status: ClearStatus
    detail: str = ""

    @property
    def ok(self) -> bool:
        return self.status.ok

    def as_dict(self) -> dict:
        return {"slot": self.slot, "status": self.status.value, "detail": self.detail}


def preparation_gestures(slot_number: int) -> list[tuple[str, int]]:
    """The scoped preparation sequence as data, so the order is testable without a device.

    Selecting the track is the caller's job because the driver already owns the
    track press; the clear is that slot's own loop-button long press.
    """
    if not 1 <= slot_number <= 8:
        raise ValueError(f"slot number must be 1-8, got {slot_number}")
    return [("clear_slot_long_press", slot_number)]


def classify_clear_evidence(
    lines: list[str],
    after_index: int,
    *,
    track_index0: int,
    slot0: int,
) -> ClearOutcome:
    """Classify the firmware's clear evidence for one requested target.

    A refusal (`deferred save temporarily blocked`) and an abort are distinct
    non-successes; an ignored empty target is success because the precondition
    already holds. Evidence naming a different target is `WRONG_TARGET`.
    """
    slot_number = slot0 + 1
    for line in lines[after_index:]:
        if "Clear refused" in line:
            return ClearOutcome(slot_number, ClearStatus.REFUSED, line.strip())
        if "Clear aborted" in line:
            return ClearOutcome(slot_number, ClearStatus.ABORTED, line.strip())
        ignored = _IGNORED_EMPTY.search(line)
        if ignored is not None:
            evidence = (int(ignored.group(1)), int(ignored.group(2)))
            if evidence == (track_index0, slot0):
                return ClearOutcome(slot_number, ClearStatus.ALREADY_EMPTY, line.strip())
            return ClearOutcome(slot_number, ClearStatus.WRONG_TARGET, line.strip())
        if ("long-press switch" in line or "long-press queued at loop-end" in line
                or "long-press start (not playing)" in line):
            return ClearOutcome(slot_number, ClearStatus.NOT_SELECTED, line.strip())
        if "Clear reset non-empty track state to EMPTY" in line:
            return ClearOutcome(slot_number, ClearStatus.CLEARED, line.strip())
        cleared = _CLEARED_SLOT.search(line)
        if cleared is not None:
            if int(cleared.group(1)) == slot_number:
                return ClearOutcome(slot_number, ClearStatus.CLEARED, line.strip())
            return ClearOutcome(slot_number, ClearStatus.WRONG_TARGET, line.strip())
    return ClearOutcome(slot_number, ClearStatus.UNCONFIRMED)


def _read_lines(capture_log: Path) -> list[str]:
    if not capture_log.exists():
        return []
    return capture_log.read_text(encoding="utf-8", errors="replace").splitlines()


def _long_press_recognized(lines: list[str], after_index: int, slot_number: int) -> bool:
    """The firmware logs the recognized long press before it decides what to do.

    A selected slot with no content is a deliberate silent no-op, so this row is
    what lets that silence be read as `ALREADY_EMPTY` instead of a lost gesture.
    """
    marker = f"Button press: Loop {slot_number} (long)"
    return any(marker in line for line in lines[after_index:])


def clear_slot(
    action_ctx: ActionContext,
    capture_log: Path,
    *,
    track_index0: int,
    slot0: int,
    long_press_ms: int = DEFAULT_LONG_PRESS_MS,
    noop_settle_s: float = 1.5,
    timeout_s: float = DEFAULT_CLEAR_TIMEOUT_S,
    lines_reader: Optional[Callable[[Path], list[str]]] = None,
    sleep: Callable[[float], None] = time.sleep,
    now: Callable[[], float] = time.monotonic,
) -> ClearOutcome:
    """Long-press one authorized slot's own record/overdub button to clear it.

    Transport must already be stopped. Measured firmware semantics
    (`MidiButtonActions`, `CLEAR_TRACK_FOR_SLOT`):
      - target IS the selected slot and has data  -> clear, logs the slot;
      - target IS the selected slot and is empty  -> deliberate silent no-op;
      - target is NOT selected and has data       -> switches playback instead of
        clearing, and must not be read as a clear;
      - a loop-button *short* press is a record toggle (it arms/starts), so it is
        never used here as a harmless "select".

    `long_press_ms` is the clear long press and must exceed the firmware's
    short/long discriminator. A recognized long press with no clear evidence is
    reported as `ALREADY_EMPTY` (selected empty no-op, or an unselected empty
    target); a switch row is `NOT_SELECTED`; an unrecognized press is
    `UNCONFIRMED`. Bounded by `timeout_s`.
    """
    if long_press_ms < slots.DEFAULT_LONG_PRESS_MS:
        raise ValueError(
            f"scoped clear needs the long press (>= {slots.DEFAULT_LONG_PRESS_MS} ms), "
            f"got {long_press_ms} ms")
    read = lines_reader or _read_lines
    before = len(read(capture_log))
    slots.press_slot_clear(action_ctx, slot0 + 1, press_ms=long_press_ms)
    recognized_at: Optional[float] = None
    deadline = now() + timeout_s
    while now() < deadline:
        lines = read(capture_log)
        outcome = classify_clear_evidence(lines, before, track_index0=track_index0, slot0=slot0)
        if outcome.status is not ClearStatus.UNCONFIRMED:
            return outcome
        if recognized_at is None and _long_press_recognized(lines, before, slot0 + 1):
            recognized_at = now()
        if recognized_at is not None and now() - recognized_at >= noop_settle_s:
            return ClearOutcome(slot0 + 1, ClearStatus.ALREADY_EMPTY,
                                "recognized long press on the selected slot: empty no-op")
        sleep(0.05)
    return ClearOutcome(slot0 + 1, ClearStatus.UNCONFIRMED)
