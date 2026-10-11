"""NOTE_EDIT entry must use the newest acknowledgement and never retry blindly."""
import unittest
from unittest.mock import patch

from hitl.scenarios.edit_minimal import _ensure_note_edit_entered


class Collector:
    def __init__(self, lines):
        self.lines = list(lines)

    def snapshot(self):
        return list(self.lines)


class NoteEditEntryAckTests(unittest.TestCase):
    def run_entry(self, initial, replies):
        collector = Collector(initial)
        presses = []
        now = [0.0]

        def sleep(seconds):
            now[0] += max(seconds, 0.05)

        def press(*args, **kwargs):
            presses.append(kwargs['note'])
            collector.lines.extend(replies.pop(0) if replies else [])

        with patch('hitl.midi_io._send_short_press', side_effect=press), \
                patch('hitl.scenarios.edit_minimal.time.sleep', side_effect=sleep), \
                patch('hitl.scenarios.edit_minimal.time.monotonic', side_effect=lambda: now[0]):
            result = _ensure_note_edit_entered(None, collector, press_ms=120,
                phase_wait_ms=0, timeout_s=0.15, log_prefix='test')
        return result, presses

    def test_new_loop_mode_overrides_legacy_entry(self):
        self.assertEqual((True, [38]), self.run_entry(
            ['entered note edit mode', 'Edit session: LOOP_EDIT'],
            [['Edit session: NOTE_EDIT']]))

    def test_exit_overrides_previous_note_session(self):
        self.assertEqual((True, [38]), self.run_entry(
            ['Edit session: NOTE_EDIT', 'MIDI Encoder: Long press - exited edit mode'],
            [['Edit session: NOTE_EDIT']]))

    def test_missing_ack_does_not_retry_toggle(self):
        self.assertEqual((False, [38]), self.run_entry(['Edit session: LOOP_EDIT'], [[]]))

    def test_fresh_loop_response_allows_second_attempt(self):
        self.assertEqual((True, [38, 38]), self.run_entry([], [
            ['Edit session: LOOP_EDIT'], ['Edit session: NOTE_EDIT']]))

    def test_latest_suffix_mode_not_earlier_note_ack_decides(self):
        self.assertEqual((False, [38, 38]), self.run_entry([], [
            ['Edit session: NOTE_EDIT', 'Edit session: LOOP_EDIT'], []]))

    def test_already_note_mode_does_not_toggle(self):
        self.assertEqual((True, []), self.run_entry(['Edit session: NOTE_EDIT'], []))

    def test_legacy_entry_still_confirms(self):
        self.assertEqual((True, [38]), self.run_entry([], [['entered note edit mode']]))

class LiveGuardBudgetTests(unittest.TestCase):
    def test_initial_observation_uses_existing_setup_budget(self):
        from types import SimpleNamespace
        from hitl.scenarios.note_edit_reselect_stacked import _ensure_target_playing
        budgets = []

        def observe(*, timeout_s):
            budgets.append(timeout_s)
            return dict(track=7, slot=3, state='PLAYING', transport=1) if timeout_s >= 1 else None

        with patch('hitl.managed_capture.active_managed_capture',
                   return_value=SimpleNamespace(selected_track_status=observe)), \
                patch('hitl.midi_io._send_short_press') as press:
            ready = _ensure_target_playing(None, None, press_ms=120, phase_wait_ms=500,
                log_prefix='test', track_number=8, loop_slot=4)
        self.assertTrue(ready)
        self.assertEqual([2.0], budgets)
        press.assert_not_called()

    def test_missing_post_press_reply_does_not_abandon_remaining_budget(self):
        from types import SimpleNamespace
        from hitl.scenarios.note_edit_reselect_stacked import _ensure_target_playing
        replies = [dict(track=7, slot=3, state='STOPPED', transport=0), None,
                   dict(track=7, slot=3, state='PLAYING', transport=1)]
        with patch('hitl.managed_capture.active_managed_capture', return_value=SimpleNamespace(
                selected_track_status=lambda **kw: replies.pop(0))), \
                patch('hitl.midi_io._send_short_press') as press:
            ready = _ensure_target_playing(None, None, press_ms=120, phase_wait_ms=500,
                log_prefix='test', track_number=8, loop_slot=4)
        self.assertTrue(ready)
        self.assertEqual(1, press.call_count)


class FencedDeadlineTests(unittest.TestCase):
    def test_complete_reply_at_deadline_is_read_before_timeout(self):
        import tempfile
        from pathlib import Path
        from hitl.managed_capture import ManagedCaptureSession
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'session.log'
            log.write_text('')
            session = ManagedCaptureSession.__new__(ManagedCaptureSession)
            session._session_log = log
            session._pending_command_nonce = 'a' * 32
            now = [0.0]

            def sleep(seconds):
                now[0] += seconds
                log.write_text('#CAP,1,TRACE_FENCE,' + 'a' * 32 + '\n'
                    '#TRACK_STATUS,us=2,track=7,slot=3,state=PLAYING,transport=1\n'
                    '#CAP,3,TRACE_FENCE,' + 'b' * 32 + '\n')

            with patch('hitl.managed_capture.time.monotonic', side_effect=lambda: now[0]), \
                    patch('hitl.managed_capture.time.sleep', side_effect=sleep):
                rows = session.fenced_response('a' * 32, 'b' * 32,
                                               timeout_s=0.05, poll_s=0.05)
            self.assertEqual(['#TRACK_STATUS,us=2,track=7,slot=3,state=PLAYING,transport=1'], rows)
            self.assertIsNone(session.pending_command_nonce)



if __name__ == '__main__':
    unittest.main()
