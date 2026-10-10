"""The note-edit lane's setup guard must judge transport state on a caught-up serial view.

`_ensure_target_playing` refuses the leg (SETUP_INCOMPLETE) when the device does not report
PLAYING, and it presses the transport exactly once when it does not. Both decisions are read
from the followed serial rows, so a snapshot taken while that view is still working through a
burst is a stale observation: `captures/live-current-root-entry-t8s4-14` refused with
`state=STOPPED` although the device's own rows carried `ST,Track,STOPPED,PLAYING` 0.43 s after
the press. These fixtures hold the collector behind the device's rows and require the guard to
let the view catch up before it judges, and to press at most once.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hitl.scenarios.note_edit_reselect_stacked import _ensure_target_playing
class LaggingFollowView:
    """A followed serial view that shows the device's new rows only once it is asked to catch up.

    `drain_until_idle` is the collector's own catch-up primitive: in the real pipeline its line
    count keeps moving while a burst is still being read and stops once the view has reached it.
    Here the same call is what makes the rows after the press visible, so a guard that judges
    without catching up reads the pre-press rows forever.
    """

    def __init__(self, *, plays_after_press: bool):
        self.plays_after_press = plays_after_press
        self.press_started = False
        self.drains = 0
        self._lines = ["#CAP,2223434672,ST,Track,PLAYING,STOPPED"]

    def snapshot(self) -> list[str]:
        return list(self._lines)

    def line_count(self) -> int:
        return len(self._lines)

    def capture_integrity(self) -> str:
        return "COMPLETE"

    def drain_until_idle(self, *, idle_ms: int, max_drain_ms: int) -> int:
        self.drains += 1
        before = len(self._lines)
        if self.plays_after_press and self.press_started:
            self._lines = ["#CAP,2267708851,ST,Track,STOPPED,PLAYING"]
        return len(self._lines) - before


class PressRecordingPort:
    def __init__(self):
        self.messages = []

    def send(self, message) -> None:
        self.messages.append(message)


class Clock:
    """A monotonic source the guard's bounded wait can be driven through deterministically."""

    def __init__(self, values):
        self._values = list(values)
        self._last = self._values[-1]

    def monotonic(self):
        return self._values.pop(0) if self._values else self._last

    def sleep(self, _seconds):
        return None


class TargetPlayingGuardTests(unittest.TestCase):
    def _press_side_effect(self, collector, presses):
        def press(*_args, **kwargs):
            collector.press_started = True   # the device needs the press before it can play
            presses.append(kwargs.get("note"))
        return press

    def test_guard_waits_for_a_lagging_view_before_refusing(self) -> None:
        collector = LaggingFollowView(plays_after_press=True)
        presses = []
        with patch("hitl.midi_io._send_short_press",
                   side_effect=self._press_side_effect(collector, presses)):
            ready = _ensure_target_playing(PressRecordingPort(), collector,
                                           press_ms=120, phase_wait_ms=500,
                                           log_prefix="[test]")
        self.assertTrue(ready)
        self.assertGreaterEqual(collector.drains, 1)
        self.assertEqual([39], presses)

    def test_guard_does_not_press_a_take_that_is_already_playing(self) -> None:
        collector = LaggingFollowView(plays_after_press=False)
        collector._lines = ["#CAP,2267708851,ST,Track,STOPPED,PLAYING"]
        presses = []
        with patch("hitl.midi_io._send_short_press",
                   side_effect=self._press_side_effect(collector, presses)):
            ready = _ensure_target_playing(PressRecordingPort(), collector,
                                           press_ms=120, phase_wait_ms=500,
                                           log_prefix="[test]")
        self.assertTrue(ready)
        self.assertEqual([], presses)

    def test_guard_presses_once_and_refuses_when_a_caught_up_view_never_plays(self) -> None:
        collector = LaggingFollowView(plays_after_press=False)
        presses = []
        with patch("hitl.midi_io._send_short_press",
                   side_effect=self._press_side_effect(collector, presses)), \
                patch("hitl.scenarios.note_edit_reselect_stacked.time",
                      Clock([0.0, 0.0, 0.5, 1.0, 1.5, 2.0, 2.5])):
            ready = _ensure_target_playing(PressRecordingPort(), collector,
                                           press_ms=120, phase_wait_ms=100,
                                           log_prefix="[test]")
        self.assertFalse(ready)
        self.assertEqual([39], presses)


if __name__ == "__main__":
    unittest.main()
