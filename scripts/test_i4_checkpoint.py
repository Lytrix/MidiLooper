"""Fail-closed checkpoint qualification before any I4 scratch mutation."""
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

from hitl.scenarios.i4_checkpoint import checkpoint, verify_checkpoint


def receipts():
    return [
        '#CAP,101,LDSV,1', '#CAP,201,OVLY,sel,0,0',
        '#CAP,301,OVLY,confirm,0,0', '#CAP,302,PERS,rev_request,0,0,0,queued',
        '#CAP,303,LDSV,0', '#CAP,304,PERS,rev_overlay_save,0,0,0,queued_exit',
        '#CAP,305,PERS,rev_dispatch,0,0,0,run',
        '#CAP,306,PERS,rev_complete,0,3,7,S0003_v0007',
    ]


class CheckpointOracleTests(unittest.TestCase):
    def verify(self, lines):
        return verify_checkpoint(lines, enter_floor=100, row_floor=200, save_floor=300)

    def test_exact_identity(self):
        result = self.verify(receipts())
        self.assertTrue(result['ok'], result)
        self.assertEqual(result['checkpoint'], {'set_id': 3, 'revision_id': 7, 'label': 'S0003_v0007'})

    def test_every_required_receipt_is_required(self):
        for index in range(len(receipts())):
            with self.subTest(index=index):
                lines = receipts(); del lines[index]
                self.assertFalse(self.verify(lines)['ok'])

    def test_late_drained_old_commit_refused(self):
        lines = receipts(); lines[-1] = '#CAP,299,PERS,rev_complete,0,3,7,S0003_v0007'
        self.assertFalse(self.verify(lines)['ok'])

    def test_old_overlay_or_row_refused(self):
        for index, stamp in ((0, 99), (1, 199)):
            lines = receipts(); lines[index] = lines[index].replace(lines[index].split(',')[1], str(stamp), 1)
            self.assertFalse(self.verify(lines)['ok'])

    def test_wrong_row_refused(self):
        lines = receipts(); lines[1] = '#CAP,201,OVLY,sel,0,1'
        self.assertFalse(self.verify(lines)['ok'])

    def test_changed_row_before_confirm_refused(self):
        lines = receipts(); lines.insert(2, '#CAP,202,OVLY,sel,0,1')
        self.assertFalse(self.verify(lines)['ok'])

    def test_conflicting_identity_refused(self):
        lines = receipts(); lines[-1] = '#CAP,306,PERS,rev_complete,0,3,7,S0004_v0007'
        self.assertFalse(self.verify(lines)['ok'])

    def test_ambiguous_commit_refused(self):
        self.assertFalse(self.verify(receipts() + ['#CAP,307,PERS,rev_complete,0,3,8,S0003_v0008'])['ok'])

    def test_dispatch_after_completion_refused(self):
        lines = receipts(); lines[-2] = '#CAP,309,PERS,rev_dispatch,0,0,0,run'
        self.assertFalse(self.verify(lines)['ok'])


class CheckpointDriverTests(unittest.TestCase):
    def setUp(self):
        self.collector = Mock(); self.collector.snapshot.return_value = ['#CAP,50,LDSV,0']
        self.action = SimpleNamespace(collector=self.collector,
            config=SimpleNamespace(track_number=8, loop_slot=4), out_port=Mock())
        self.capture = Mock()

    def test_missing_resources_refuse_before_send(self):
        for capture, collector in ((None, self.collector), (self.capture, None)):
            self.action.collector = collector
            with patch('hitl.scenarios.i4_checkpoint.press_edit_double') as send:
                with self.assertRaises(RuntimeError): checkpoint(self.action, capture)
                send.assert_not_called()

    def test_unauthorized_target_refuses_before_send(self):
        for track, slot in ((7, 4), (8, 3)):
            self.action.config.track_number, self.action.config.loop_slot = track, slot
            with patch('hitl.scenarios.i4_checkpoint.press_edit_double') as send:
                with self.assertRaises(RuntimeError): checkpoint(self.action, self.capture)
                send.assert_not_called()

    def test_failed_enter_never_scrolls_or_saves_or_retries(self):
        with patch('hitl.scenarios.i4_checkpoint.fresh_floor', return_value=100), \
             patch('hitl.scenarios.i4_checkpoint.press_edit_double') as enter, \
             patch('hitl.scenarios.i4_checkpoint.press_overlay_up') as scroll, \
             patch('hitl.scenarios.i4_checkpoint.press_edit') as save:
            with self.assertRaises(RuntimeError): checkpoint(self.action, self.capture, wait=Mock(return_value=False))
            enter.assert_called_once(); scroll.assert_not_called(); save.assert_not_called()

    def test_successful_driver_uses_only_three_gestures(self):
        def wait(rows, markers, **kwargs):
            self.collector.snapshot.return_value = receipts()
            return any(marker in line for line in rows() for marker in markers)
        with patch('hitl.scenarios.i4_checkpoint.fresh_floor', side_effect=[100, 200, 300]), \
             patch('hitl.scenarios.i4_checkpoint.press_edit_double') as enter, \
             patch('hitl.scenarios.i4_checkpoint.press_overlay_up') as scroll, \
             patch('hitl.scenarios.i4_checkpoint.press_edit') as save:
            result = checkpoint(self.action, self.capture, wait=wait)
            self.assertTrue(result['ok'], result)
            enter.assert_called_once(); scroll.assert_called_once(); save.assert_called_once()


class CheckpointPreconditionTests(unittest.TestCase):
    def test_stopped_floor_is_fenced_status_timestamp(self):
        from hitl.scenarios.i4_checkpoint import fresh_floor
        capture = Mock()
        capture.selected_track_status.return_value = dict(us=123, transport=0, state='STOPPED')
        self.assertEqual(fresh_floor(capture), 123)
        for status in (None, dict(us=123, transport=1, state='PLAYING'),
                       dict(us=123, transport=0, state='ARMED')):
            capture.selected_track_status.return_value = status
            with self.assertRaises(RuntimeError): fresh_floor(capture)

    def test_actual_action_wire_mapping(self):
        from hitl.actions.buttons import press_edit_double, press_overlay_up
        ctx = SimpleNamespace(out_port=Mock(), config=SimpleNamespace(press_ms=120))
        with patch('hitl.actions.buttons._send_multi_short_press') as double, \
             patch('hitl.actions.buttons._send_short_press') as short:
            press_edit_double(ctx); press_overlay_up(ctx)
            double.assert_called_once_with(ctx.out_port, note=38, channel_1based=16,
                press_ms=120, count=2, gap_ms=80)
            short.assert_called_once_with(ctx.out_port, note=37, channel_1based=16, press_ms=120)

    def test_unknown_or_open_overlay_refused(self):
        for lines in ([], ['#CAP,99,LDSV,1']):
            collector = Mock(); collector.snapshot.return_value = lines
            action = SimpleNamespace(collector=collector, config=SimpleNamespace(track_number=8, loop_slot=4))
            with patch('hitl.scenarios.i4_checkpoint.fresh_floor', return_value=100), \
                 patch('hitl.scenarios.i4_checkpoint.press_edit_double') as enter:
                with self.assertRaises(RuntimeError): checkpoint(action, Mock())
                enter.assert_not_called()

    def test_row_changes_after_wait_refuse_before_save(self):
        collector = Mock(); collector.snapshot.side_effect = [
            ['#CAP,50,LDSV,0'], ['#CAP,201,OVLY,sel,0,0', '#CAP,202,OVLY,sel,0,1']]
        action = SimpleNamespace(collector=collector, config=SimpleNamespace(track_number=8, loop_slot=4))
        with patch('hitl.scenarios.i4_checkpoint.fresh_floor', side_effect=[100, 200, 300]), \
             patch('hitl.scenarios.i4_checkpoint.press_edit_double'), \
             patch('hitl.scenarios.i4_checkpoint.press_overlay_up'), \
             patch('hitl.scenarios.i4_checkpoint.press_edit') as save:
            with self.assertRaises(RuntimeError): checkpoint(action, Mock(), wait=Mock(return_value=True))
            save.assert_not_called()

class CheckpointReviewRegressionTests(unittest.TestCase):
    def test_five_digit_firmware_identity(self):
        lines = receipts(); lines[-1] = '#CAP,306,PERS,rev_complete,0,10000,65535,S10000_v65535'
        result = verify_checkpoint(lines, enter_floor=100, row_floor=200, save_floor=300)
        self.assertTrue(result['ok'], result)

    def test_owner_closed_precondition_accepts_no_ldsv_history(self):
        collector = Mock(); collector.snapshot.return_value = []
        action = SimpleNamespace(collector=collector, config=SimpleNamespace(track_number=8, loop_slot=4))
        def wait(rows, markers, **kwargs):
            collector.snapshot.return_value = receipts()
            return True
        with patch('hitl.scenarios.i4_checkpoint.fresh_floor', side_effect=[100, 200, 300]), \
             patch('hitl.scenarios.i4_checkpoint.press_edit_double'), \
             patch('hitl.scenarios.i4_checkpoint.press_overlay_up'), \
             patch('hitl.scenarios.i4_checkpoint.press_edit'):
            self.assertTrue(checkpoint(action, Mock(), panel_overlay_closed=True, wait=wait)['ok'])

    def test_partial_midi_acquisition_closes_output(self):
        from hitl.session import open_midi_session
        output = Mock()
        with patch('hitl.session.mido') as mido, patch('hitl.midi_io._find_midi_port', return_value='Teensy'):
            mido.open_output.return_value = output
            mido.open_input.side_effect = OSError('input unavailable')
            with self.assertRaises(OSError):
                open_midi_session(midi_out_name='Teensy', midi_in_name='Teensy', serial_port=None)
        output.close.assert_called_once()



    def test_midi_open_failure_persists_report_and_stops_follower(self):
        import json
        import os
        import tempfile
        from pathlib import Path
        from hitl.scenarios.i4_checkpoint import run_i4_checkpoint
        with tempfile.TemporaryDirectory(dir=os.environ.get('PI_SCRATCH_DIR')) as directory:
            config = SimpleNamespace(track_number=8, loop_slot=4, out_dir=Path(directory),
                                     midi_out_name='Teensy', midi_in_name='Teensy')
            capture = SimpleNamespace(session_log=Path(directory) / 'session.log')
            with patch('hitl.layered_cli.config_from_runner_args', return_value=config), \
                 patch('hitl.managed_capture.active_managed_capture', return_value=capture), \
                 patch('hitl.serial_follow.ExternalSerialFollowCollector') as follower, \
                 patch('hitl.scenarios.i4_checkpoint.wait_for_boot_handoff', return_value='not-fresh-boot'), \
                 patch('hitl.session.open_midi_session', side_effect=OSError('input unavailable')):
                self.assertEqual(run_i4_checkpoint(SimpleNamespace(legacy_args=[])), 2)
                follower.return_value.stop.assert_called_once()
            report = json.loads((Path(directory) / 'i4-checkpoint.json').read_text())
            self.assertEqual(report['verdict'], 'SETUP_INCOMPLETE')
            self.assertIn('input unavailable', report['issues'][0])

if __name__ == '__main__':
    unittest.main()
