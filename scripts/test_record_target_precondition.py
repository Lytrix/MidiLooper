"""No-clear record arm must select the configured slot, then verify it.

``--no-clear-before-record`` authorises skipping the clear, not the selection: the
clear path selects the Loops row, so the no-clear path must select the target and
prove it empty before the arm press (2026-09-25 target mismatch: configured
track 8 slot 4, armed slot 1).
"""
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

from hitl import legacy_record_baseline as baseline
from hitl.control_constants import RECORD_BUTTON_NOTE
from hitl.legacy_record_baseline import (
    LOOP_SELECT_NOTE_BASE,
    _await_observed_record_slot,
    _require_observed_record_slot,
    _run_record_arm_until_capturing,
    _select_loop_slot_before_record_arm,
)


def _arm_args(**overrides) -> SimpleNamespace:
    values = dict(
        press_ms=120,
        phase_wait_ms=500,
        state_sync_timeout_ms=2500,
        clear_before_record=False,
    )
    values.update(overrides)
    return SimpleNamespace(**values)


def _collector(lines):
    collector = Mock()
    collector.snapshot.side_effect = lambda: list(lines)
    return collector


def _abort():
    abort = Mock()
    abort.check.return_value = None
    return abort


def _notes(send: Mock) -> list[int]:
    return [call.kwargs["note"] for call in send.call_args_list]


class SelectSlotBeforeArmTest(unittest.TestCase):
    def _run_select(self, lines, *, loop_slot=4, clear_before_record=False):
        collector = _collector(lines)
        with patch.object(baseline, "_send_short_press") as send, \
             patch.object(baseline.time, "sleep"), \
             patch.object(
                 baseline, "_ensure_transport_stopped_before_record_arm", return_value=False
             ) as stop, \
             patch.object(
                 baseline, "_stop_empty_slot_select_recording_before_clear"
             ) as stop_ghost:
            result = _select_loop_slot_before_record_arm(
                Mock(),
                Mock(),
                collector,
                _arm_args(clear_before_record=clear_before_record),
                loop_slot=loop_slot,
                abort=_abort(),
            )
        return result, send, stop, stop_ghost

    def test_selects_loops_row_for_the_configured_slot(self):
        baseline_len, send, stop, stop_ghost = self._run_select(
            ["#CAP,100,DISP,0,EMPTY,0"]
        )
        self.assertEqual(1, send.call_count)
        self.assertEqual(LOOP_SELECT_NOTE_BASE + 3, send.call_args.kwargs["note"])
        self.assertEqual(1, baseline_len)
        self.assertEqual(1, stop.call_count)
        self.assertEqual(1, stop_ghost.call_count)

    def test_clear_path_selects_without_a_second_transport_stop(self):
        """The clear step owns the transport stop; selection must not re-order it."""
        _, send, stop, _ = self._run_select(
            ["#CAP,100,DISP,0,EMPTY,0"], clear_before_record=True
        )
        stop.assert_not_called()
        self.assertEqual(1, send.call_count)

    def test_transport_stop_is_requested_before_the_select_press(self):
        """Selecting under a running transport starts a ghost capture whose later
        stop leaves ARMED, skipping the arm press and stranding the take."""
        order: list[str] = []

        def _stop(*_args, **_kwargs):
            order.append("transport_stop")
            return False

        def _send(*_args, **kwargs):
            order.append(f"press:{kwargs.get('note')}")

        collector = _collector(["#CAP,100,DISP,0,EMPTY,0"])
        with patch.object(baseline, "_send_short_press", side_effect=_send), \
             patch.object(baseline.time, "sleep"), \
             patch.object(baseline, "_ensure_transport_stopped_before_record_arm", side_effect=_stop), \
             patch.object(baseline, "_stop_empty_slot_select_recording_before_clear"):
            _select_loop_slot_before_record_arm(
                Mock(), Mock(), collector, _arm_args(), loop_slot=4, abort=_abort()
            )
        self.assertEqual(["transport_stop", f"press:{LOOP_SELECT_NOTE_BASE + 3}"], order)

    def test_baseline_is_taken_before_the_select_press(self):
        lines = ["#CAP,100,DISP,0,EMPTY,0", "#CAP,200,ST,Track,STOPPED,EMPTY,0"]
        baseline_len, _, _, stop_ghost = self._run_select(lines)
        self.assertEqual(len(lines), baseline_len)
        self.assertEqual(len(lines), stop_ghost.call_args.kwargs["after_index"])

    def test_selection_proof_is_the_slot_not_an_empty_state(self):
        """A stopped slot reports ARMED after selection; the tile moved is the proof."""
        collector = _collector(["#CAP,100,DISP,3,ARMED,0"])
        with patch.object(baseline, "_send_short_press"), \
             patch.object(baseline.time, "sleep"), \
             patch.object(baseline, "_ensure_transport_stopped_before_record_arm"), \
             patch.object(baseline, "_stop_empty_slot_select_recording_before_clear"):
            _select_loop_slot_before_record_arm(
                Mock(), Mock(), collector, _arm_args(), loop_slot=4, abort=_abort()
            )
        # The caller's guard reads the same window the selector produced.
        _require_observed_record_slot(collector.snapshot(), 4, 0)

    def test_a_select_that_does_not_move_the_slot_fails_closed(self):
        collector = _collector(["#CAP,100,DISP,0,ARMED,0"])
        with patch.object(baseline, "_send_short_press"), \
             patch.object(baseline.time, "sleep"), \
             patch.object(baseline, "_ensure_transport_stopped_before_record_arm"), \
             patch.object(baseline, "_stop_empty_slot_select_recording_before_clear"):
            _select_loop_slot_before_record_arm(
                Mock(), Mock(), collector, _arm_args(), loop_slot=4, abort=_abort()
            )
            with self.assertRaises(RuntimeError):
                _require_observed_record_slot(collector.snapshot(), 4, 0)

    def test_zero_slot_is_a_no_op(self):
        _, send, stop, _ = self._run_select([], loop_slot=0)
        send.assert_not_called()
        stop.assert_not_called()


class ObservedSlotWaitTest(unittest.TestCase):
    """The stopped display emits on change, so the proof must be waited for."""

    def _collector(self, snapshots):
        collector = Mock()
        collector.snapshot.side_effect = [list(item) for item in snapshots]
        return collector

    def test_waits_for_a_late_display_row(self):
        collector = self._collector([
            ["#CAP,100,DISP,0,ARMED,0"],
            ["#CAP,100,DISP,0,ARMED,0"],
            ["#CAP,100,DISP,0,ARMED,0", "#CAP,200,DISP,3,ARMED,0"],
        ])
        with patch.object(baseline.time, "sleep"):
            _await_observed_record_slot(
                collector, loop_slot=4, after_index=0, timeout_s=1.0, abort=_abort()
            )

    def test_quiet_display_without_the_target_refuses(self):
        collector = Mock()
        collector.snapshot.return_value = ["#CAP,100,DISP,0,ARMED,0"]
        with patch.object(baseline.time, "sleep"), \
             patch.object(baseline.time, "monotonic", side_effect=[0.0, 0.0, 5.0]):
            with self.assertRaisesRegex(RuntimeError, "latest observed slot 1"):
                _await_observed_record_slot(
                    collector, loop_slot=4, after_index=0, timeout_s=1.0, abort=_abort()
                )

    def test_abort_stops_the_wait(self):
        collector = Mock()
        collector.snapshot.return_value = ["#CAP,100,DISP,0,ARMED,0"]
        abort = Mock()
        abort.check.return_value = "user aborted"
        with patch.object(baseline.time, "sleep"):
            with self.assertRaises(RuntimeError):
                _await_observed_record_slot(
                    collector, loop_slot=4, after_index=0, timeout_s=30.0, abort=abort
                )


class NoClearTargetPreconditionTest(unittest.TestCase):
    def test_matching_latest_slot(self):
        _require_observed_record_slot(['#CAP,100,DISP,3,EMPTY'], 4, 0)

    def test_mismatch_refuses_even_with_older_matching_row(self):
        with self.assertRaisesRegex(RuntimeError, 'latest observed slot 1'):
            _require_observed_record_slot(['#CAP,100,DISP,3,EMPTY', '#CAP,200,DISP,0,EMPTY'], 4, 0)

    def test_device_time_orders_ring_rows(self):
        _require_observed_record_slot(['#CAP,200,DISP,3,EMPTY', '#CAP,100,DISP,0,EMPTY'], 4, 0)

    def test_absent_malformed_and_previous_track_evidence_refuses(self):
        for lines, baseline in [([], 0), (['#CAP,bad,DISP,3,EMPTY'], 0), (['#CAP,100,DISP,3,EMPTY'], 1)]:
            with self.assertRaises(RuntimeError):
                _require_observed_record_slot(lines, 4, baseline)

    def test_arm_selects_before_transport_stop_and_press(self):
        """The arm path must select the Loops row before stopping transport/arming."""
        order: list[str] = []
        collector = _collector(['#CAP,100,DISP,3,STOPPED,0'])

        def _select(*_args, **_kwargs):
            order.append("select")
            return 0

        def _stop(*_args, **_kwargs):
            order.append("transport_stop")
            return False

        def _send(*_args, **kwargs):
            order.append(f"press:{kwargs.get('note')}")

        def _request(send_request, **_kwargs):
            order.append("arm")
            send_request()
            order.append("arm_return")
            return SimpleNamespace(ok=True, evidence=SimpleNamespace(baseline_index=0))

        with patch.object(baseline, "_select_loop_slot_before_record_arm", side_effect=_select), \
             patch.object(baseline, "_ensure_transport_stopped_before_record_arm", side_effect=_stop), \
             patch.object(baseline, "_stop_live_capture_if_active_before_arm", return_value=True), \
             patch.object(baseline, "_send_short_press", side_effect=_send), \
             patch.object(baseline, "request_state_transition", side_effect=_request), \
             patch.object(baseline, "log_action_outcome"), \
             patch.object(baseline, "_serial_has_recording_active", return_value=True):
            reached, started = _run_record_arm_until_capturing(
                Mock(), Mock(), collector, _arm_args(),
                track_baseline_len=0, record_loop_slot=4, abort=_abort(),
            )

        self.assertTrue(reached)
        self.assertIsNotNone(started)
        self.assertEqual(
            ["select", "transport_stop", "arm", f"press:{RECORD_BUTTON_NOTE}", "arm_return"],
            order,
        )

    def test_arm_refuses_a_mismatched_target_after_selection(self):
        """Selection that does not move the device slot fails closed, without an arm press."""
        collector = _collector(['#CAP,100,DISP,0,EMPTY,0'])
        with patch.object(baseline, "_select_loop_slot_before_record_arm", return_value=0), \
             patch.object(baseline, "_ensure_transport_stopped_before_record_arm") as stop, \
             patch.object(baseline, "_send_short_press") as send:
            with self.assertRaises(RuntimeError):
                _run_record_arm_until_capturing(
                    Mock(), Mock(), collector, _arm_args(),
                    track_baseline_len=0, record_loop_slot=4, abort=_abort(),
                )
        send.assert_not_called()
        stop.assert_not_called()

    def test_arm_skips_selection_when_the_caller_already_selected(self):
        """The edit path selects inline; a second Loops-row tap must not be sent."""
        collector = _collector(['#CAP,100,DISP,3,STOPPED,0'])
        with patch.object(baseline, "_select_loop_slot_before_record_arm") as select, \
             patch.object(baseline, "_ensure_transport_stopped_before_record_arm", return_value=False), \
             patch.object(baseline, "_stop_live_capture_if_active_before_arm", return_value=True), \
             patch.object(baseline, "_send_short_press"), \
             patch.object(
                 baseline, "request_state_transition",
                 return_value=SimpleNamespace(
                     ok=False, evidence=SimpleNamespace(baseline_index=0)
                 ),
             ), \
             patch.object(baseline, "log_action_outcome"), \
             patch.object(baseline, "_serial_has_recording_active", return_value=True):
            _run_record_arm_until_capturing(
                Mock(), Mock(), collector, _arm_args(),
                track_baseline_len=0, record_loop_slot=4, abort=_abort(),
                select_loop_slot=False,
            )
        select.assert_not_called()


if __name__ == '__main__':
    unittest.main()
