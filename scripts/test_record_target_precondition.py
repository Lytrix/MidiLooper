"""No-clear record arm must verify device-selected slot before sending gestures."""
import unittest
from types import SimpleNamespace
from unittest.mock import Mock
from hitl.legacy_record_baseline import _require_observed_record_slot, _run_record_arm_until_capturing


class RecordTargetPreconditionTest(unittest.TestCase):
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

    def test_production_arm_sends_nothing_on_mismatch(self):
        out = Mock()
        serial = Mock()
        serial.snapshot.return_value = ['#CAP,100,DISP,0,EMPTY']
        with self.assertRaises(RuntimeError):
            _run_record_arm_until_capturing(out, Mock(), serial,
                SimpleNamespace(clear_before_record=False), track_baseline_len=0,
                record_loop_slot=4, abort=Mock())
        out.assert_not_called()
        self.assertEqual([], out.mock_calls)

if __name__ == '__main__':
    unittest.main()
