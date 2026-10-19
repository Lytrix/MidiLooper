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
from hitl.scenarios.note_edit_reselect_stacked import _ensure_target_playing, _wait_select_apply
from hitl.verify.note_edit_reselect_stacked import (
    direct_select_lands_on_step,
    parse_direct_select,
)


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


class DirectSelectWitnessTests(unittest.TestCase):
    """The live select proof must be keyed to the channel the device puts on the wire.

    `#DBG select_apply …` is deferred: `Logger.cpp` queues every `#DBG` line into the capture ring
    (`src/Logger.cpp`:181), the timing-critical (PLAYING) flush transmits nothing
    (`src/Utils/DebugSessionCapture.cpp`:2081-2095) and `!TRACE_DRAIN` is transport-gated
    (`src/StorageManager/StorageManagerHitlSerial.cpp`:750). `captures/i3b-seeded-edit-t8s4-04`
    is that shape: the scenario's live wait expired after deselect attempts 1/3-3/3 although the
    device had applied the deselect, because the `#DBG` row it waited on is a drained record
    (line 10835) that reaches the log only after the transport stops. The direct
    `Select fader: …` rows are from the same apply branches (`src/ControlSurface/SelectFaderInput.cpp`
    lines 331/334 and 416), so they witness the same events -- and the rows used here are verbatim
    from that capture (lines 6766 and 10835).
    """

    DESELECT_DIRECT = (
        "[75.579] [DEBUG] [MIDI] Select fader: selected empty step at tick 0 (no note)"
    )
    NOTE_DIRECT = (
        "[75.500] [DEBUG] [MIDI] Select fader: selected note 3 at tick 528 (1/2 notes at this position)"
    )
    DESELECT_APPLIED = (
        "#DBG select_apply selected_tick=0 note_idx=-1 slot=0 prior_slot=-1 apply=1 reason=empty_step"
    )
    DESELECT_UNCHANGED = (
        "#DBG select_apply selected_tick=0 note_idx=-1 slot=0 prior_slot=-1 apply=0 reason=empty_step"
    )
    NOTE_APPLIED = (
        "#DBG select_apply selected_tick=528 note_idx=3 slot=1 prior_slot=-1 apply=1 reason=note_changed"
    )

    class _Collector:
        def __init__(self, lines):
            self._lines = list(lines)

        def snapshot(self):
            return list(self._lines)

    @staticmethod
    def _wait(lines, *, expected_tick, want_note):
        collector = DirectSelectWitnessTests._Collector(lines)
        # 0.0 starts the 2 s window, 9.0 expires it: one scan happens, then the wait gives up.
        with patch("hitl.scenarios.note_edit_reselect_stacked.time", Clock([0.0, 0.0, 9.0])):
            return _wait_select_apply(
                collector,
                expected_tick=expected_tick,
                baseline=0,
                want_note=want_note,
            )

    def test_live_deselect_wait_accepts_the_device_direct_empty_step_row(self) -> None:
        found = self._wait([self.DESELECT_DIRECT], expected_tick=0, want_note=False)
        self.assertEqual((0, -1), found)

    def test_live_deselect_wait_accepts_the_deferred_applied_row(self) -> None:
        found = self._wait([self.DESELECT_APPLIED], expected_tick=0, want_note=False)
        self.assertEqual((0, -1), found)

    def test_live_deselect_wait_rejects_the_unchanged_empty_step_row(self) -> None:
        """`apply=0` is not an apply: the strict proof is not relaxed to make the wait pass."""
        self.assertIsNone(
            self._wait([self.DESELECT_UNCHANGED], expected_tick=0, want_note=False)
        )

    def test_live_deselect_wait_rejects_another_sixteenth(self) -> None:
        wrong_direct = (
            "[75.579] [DEBUG] [MIDI] Select fader: selected empty step at tick 48 (no note)"
        )
        wrong_deferred = self.DESELECT_APPLIED.replace("selected_tick=0", "selected_tick=48")
        self.assertIsNone(
            self._wait([wrong_direct], expected_tick=0, want_note=False)
        )
        self.assertIsNone(
            self._wait([wrong_deferred], expected_tick=0, want_note=False)
        )

    def test_live_deselect_wait_rejects_a_note_at_the_same_sixteenth(self) -> None:
        self.assertIsNone(
            self._wait([self.NOTE_APPLIED], expected_tick=0, want_note=False)
        )
        self.assertIsNone(
            self._wait([self.NOTE_DIRECT], expected_tick=0, want_note=False)
        )

    def test_live_note_wait_accepts_the_device_direct_note_row(self) -> None:
        found = self._wait([self.NOTE_DIRECT], expected_tick=528, want_note=True)
        self.assertEqual((528, 3), found)

    def test_live_note_wait_accepts_an_off_grid_tick_in_the_intended_sixteenth(self) -> None:
        off_grid = "[75.500] [DEBUG] [MIDI] Select fader: selected note 3 at tick 536"
        found = self._wait([off_grid], expected_tick=528, want_note=True)
        self.assertEqual((536, 3), found)

    def test_live_note_wait_rejects_the_empty_step_row_and_wrong_sixteenth(self) -> None:
        self.assertIsNone(
            self._wait([self.DESELECT_DIRECT], expected_tick=528, want_note=True)
        )
        self.assertIsNone(
            self._wait([self.NOTE_DIRECT], expected_tick=768, want_note=True)
        )

    def test_absent_answer_still_fails(self) -> None:
        self.assertIsNone(self._wait([], expected_tick=0, want_note=False))
        self.assertIsNone(self._wait(["#CAP,100,ST,Track,PLAYING,PLAYING"], expected_tick=0,
                                     want_note=False))

    def test_parse_direct_select_and_predicate_agree(self) -> None:
        self.assertEqual((-1, 0), parse_direct_select(self.DESELECT_DIRECT))
        self.assertEqual((3, 528), parse_direct_select(self.NOTE_DIRECT))
        self.assertIsNone(parse_direct_select("#DBG select_apply selected_tick=0 note_idx=-1"))
        self.assertTrue(direct_select_lands_on_step(-1, 0, 0, want_note=False))
        self.assertFalse(direct_select_lands_on_step(-1, 0, 0, want_note=True))
        self.assertTrue(direct_select_lands_on_step(3, 536, 528, want_note=True))
        self.assertFalse(direct_select_lands_on_step(3, 576, 528, want_note=True))

if __name__ == "__main__":
    unittest.main()
