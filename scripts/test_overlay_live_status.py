"""Request-only live overlay snapshots: exact framing, freshness and refusal."""
import io
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

from hitl.serial.protocol import overlay_status
from hitl.managed_capture import ManagedCaptureSession

ROW = '#OVERLAY_STATUS,us=101,open=1,mode=2,row=0,set=1,rev=0,phase=0'


class OverlayProtocolTests(unittest.TestCase):
    def test_exact_parser_and_duplicate_refusal(self):
        self.assertEqual(dict(us=101, open=1, mode=2, row=0, set_id=1, revision_id=0, phase=0),
                         overlay_status([ROW]))
        self.assertIsNone(overlay_status([]))
        self.assertIsNone(overlay_status([ROW, ROW]))
        for before, after in [('us=101', 'us=4294967296'), ('open=1', 'open=2'),
                              ('mode=2', 'mode=5'), ('row=0', 'row=256'),
                              ('set=1', 'set=65536'), ('rev=0', 'rev=65536'),
                              ('phase=0', 'phase=4')]:
            self.assertIsNone(overlay_status([ROW.replace(before, after)]))
        self.assertIsNone(overlay_status([ROW + ',extra=1']))
        self.assertIsNone(overlay_status([ROW.replace(',row=0', '')]))

    def test_broker_only_accepts_argument_free_read_only_command(self):
        from capture_session import is_allowed_hitl_command
        self.assertTrue(is_allowed_hitl_command('!OVERLAY_STATUS'))
        for command in ('!OVERLAY_STATUS 1', '!OVERLAY_STATUS;!REV_COMMIT', '!REV_COMMIT'):
            self.assertFalse(is_allowed_hitl_command(command))

    def test_broker_sends_current_fences_around_overlay_command(self):
        import capture_session as broker
        sink = SimpleNamespace(is_open=True, writes=[])
        sink.write = sink.writes.append
        sink.flush = lambda: None
        start, end = 'a' * 32, 'b' * 32
        stream = io.StringIO(f'{start} {end} !OVERLAY_STATUS\n')
        with patch.object(broker.sys, 'stdin', stream), \
             patch.object(broker.select, 'select', return_value=([stream], [], [])):
            broker._service_command_requests(sink, enabled=True)
        self.assertEqual([f'!TRACE_FENCE {start}\n'.encode(), b'!OVERLAY_STATUS\n',
                          f'!TRACE_FENCE {end}\n'.encode()], sink.writes)

    def test_managed_query_binds_response_to_its_unique_fences(self):
        capture = SimpleNamespace(send_command=Mock(return_value=(True, 'ok')),
                                  fenced_response=Mock(return_value=[ROW]))
        with patch('hitl.managed_capture.uuid.uuid4', side_effect=[SimpleNamespace(hex='a'*32),
                                                                SimpleNamespace(hex='b'*32)]):
            result = ManagedCaptureSession.overlay_status(capture, timeout_s=1)
        capture.send_command.assert_called_once_with('!OVERLAY_STATUS', nonce='a'*32, end_nonce='b'*32)
        capture.fenced_response.assert_called_once_with('a'*32, 'b'*32, timeout_s=1)
        self.assertEqual(101, result['us'])

    def test_managed_missing_or_refused_response_has_no_fallback(self):
        for rows in (None, [], [ROW, ROW]):
            capture = SimpleNamespace(send_command=Mock(return_value=(True, 'ok')),
                                      fenced_response=Mock(return_value=rows))
            self.assertIsNone(ManagedCaptureSession.overlay_status(capture))
        capture.send_command.return_value = (False, 'refused')
        capture.fenced_response.reset_mock()
        self.assertIsNone(ManagedCaptureSession.overlay_status(capture))
        capture.fenced_response.assert_not_called()


class OverlayNavigationTests(unittest.TestCase):
    def test_live_snapshot_can_advance_without_any_cap_marker(self):
        from hitl.scenarios.i4_overlay_load import await_overlay_snapshot
        sample = overlay_status([ROW])
        capture = SimpleNamespace(overlay_status=Mock(return_value=sample))
        result = await_overlay_snapshot(capture, 100, dict(open=1, mode=2, row=0, set_id=1))
        self.assertEqual(sample, result)
        capture.overlay_status.assert_called_once()

    def test_stale_missing_wrong_or_changed_snapshot_cannot_authorize(self):
        from hitl.scenarios.i4_overlay_load import await_overlay_snapshot
        def single_poll(rows, markers, **kwargs):
            return any(marker in line for line in rows() for marker in markers)
        sample = overlay_status([ROW])
        for bad in (None, dict(sample, us=99), dict(sample, us=100), dict(sample, mode=0),
                    dict(sample, set_id=2), dict(sample, row=1), dict(sample, open=0)):
            capture = SimpleNamespace(overlay_status=Mock(return_value=bad))
            with self.assertRaises(RuntimeError):
                await_overlay_snapshot(capture, 100, dict(open=1, mode=2, row=0, set_id=1), wait=single_poll)

    def test_rollover_freshness_and_no_snapshot_only_load_pass(self):
        from hitl.scenarios.i4_overlay_load import await_overlay_snapshot, verify_load
        sample = dict(overlay_status([ROW]), us=5)
        capture = SimpleNamespace(overlay_status=Mock(return_value=sample))
        self.assertEqual(sample, await_overlay_snapshot(capture, 0xfffffff0, dict(open=1)))
        result = verify_load([], dict(set_id=1, revision_id=2, label='S0001_v0002'),
                             enter_floor=100, load_floor=200, choice_floor=300)
        self.assertFalse(result['ok'])

class OverlayFlowTests(unittest.TestCase):
    def run_fixture(self, *, changed_index=None, changed=None):
        import os
        import tempfile
        from contextlib import ExitStack
        from pathlib import Path
        from hitl.scenarios.i4_overlay_load import run_flow
        from test_i4_overlay_load import receipts, reference
        base = dict(open=1, mode=0, row=0, set_id=0, revision_id=0, phase=0)
        snapshots = [dict(base, us=101, open=0), dict(base, us=111), dict(base, us=121),
                     dict(base, us=131, row=1), dict(base, us=141, row=2),
                     dict(base, us=151, mode=2, set_id=1), dict(base, us=181, mode=2, set_id=1),
                     dict(base, us=211, mode=1, row=2, set_id=1, revision_id=2),
                     dict(base, us=212, mode=1, row=1, set_id=1, revision_id=2),
                     dict(base, us=213, mode=1, row=0, set_id=1, revision_id=2),
                     dict(base, us=221, mode=1, row=0, set_id=1, revision_id=2)]
        if changed_index is not None:
            snapshots[changed_index].update(changed)
        collector = Mock(); collector.snapshot.return_value = []
        capture = SimpleNamespace(overlay_status=Mock(side_effect=snapshots),
                                  session_log='current.log', drain_capture_ring=Mock())
        action = SimpleNamespace(config=SimpleNamespace(track_number=8, loop_slot=4, press_ms=120),
                                 collector=collector)
        def single_poll(rows, markers, **kwargs):
            return any(marker in line for line in rows() for marker in markers)
        confirm_count = 0
        def confirm(_action):
            nonlocal confirm_count
            confirm_count += 1
            if confirm_count == 2:
                collector.snapshot.return_value = receipts()
        with tempfile.TemporaryDirectory(dir=os.environ.get('PI_SCRATCH_DIR')) as directory, ExitStack() as stack:
            stack.enter_context(patch('hitl.scenarios.i4_overlay_load.live_status',
                                     return_value=dict(us=100, track=7, slot=3, state='PLAYING', transport=1)))
            proof = stack.enter_context(patch('hitl.scenarios.i4_overlay_load.await_identity',
                                             return_value=dict(approved=True)))
            sends = {}
            for name in ('press_edit_double', 'press_overlay_up', 'press_record', 'press_edit_long'):
                sends[name] = stack.enter_context(patch('hitl.scenarios.i4_overlay_load.' + name))
            stack.enter_context(patch('hitl.scenarios.i4_overlay_load.press_edit', side_effect=confirm))
            error = None; result = None
            try:
                result = run_flow(action, capture, reference()['checkpoint'], Path(directory)/'request.json',
                                  Path(directory)/'reply.json', panel_closed=True, wait=single_poll)
            except RuntimeError as exc:
                error = str(exc)
            return result, error, confirm_count, proof.call_count, sends

    def test_no_live_cap_rows_navigation_still_requires_final_receipts(self):
        result, error, confirms, proofs, sends = self.run_fixture()
        self.assertIsNone(error)
        self.assertTrue(result['ok'], result)
        self.assertEqual((2, 1), (confirms, proofs))
        self.assertEqual(11, len(result['live_overlay_snapshots']))
        self.assertEqual(3, sends['press_overlay_up'].call_count) # root clamp, dirty2->1->0

    def test_changed_history_never_sends_load(self):
        result, error, confirms, proofs, _ = self.run_fixture(changed_index=6, changed=dict(row=1))
        self.assertIn('history_focus_changed', error)
        self.assertEqual((0, 1), (confirms, proofs))

    def test_changed_dirty_selection_never_sends_yes(self):
        result, error, confirms, proofs, _ = self.run_fixture(changed_index=10, changed=dict(row=1))
        self.assertIn('dirty_yes_focus_changed', error)
        self.assertEqual(1, confirms)

    def test_snapshot_before_scroll_debounce_does_not_advance(self):
        result, error, confirms, proofs, _ = self.run_fixture(changed_index=8, changed=dict(row=2))
        self.assertIn('missing_fresh_overlay_snapshot', error)
        self.assertEqual(1, confirms)

    def test_open_overlay_precondition_never_sends_navigation(self):
        result, error, confirms, proofs, sends = self.run_fixture(changed_index=0, changed=dict(open=1))
        self.assertIn('fresh_closed_overlay', error)
        self.assertEqual((0, 0), (confirms, proofs))
        for send in sends.values():
            send.assert_not_called()



if __name__ == '__main__':
    unittest.main()
