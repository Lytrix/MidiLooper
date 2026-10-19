"""Regressions for baseline phase authority and report-after-persistence cleanup."""
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hitl.serial_timing import extract_phase_sessions
from hitl.legacy_record_baseline import (
    _managed_record_evidence_cleanup, _extract_persistence_rows, _summarize_persistence_handoff,
)


class OverdubBoundaryAuthorityTests(unittest.TestCase):
    def stop(self, lines):
        phase = extract_phase_sessions(lines).overdub
        self.assertIsNotNone(phase)
        return phase.stop_ts

    def test_record_release_during_overdub_does_not_end_input(self):
        lines = [
            '#CAP,29523982,ST,Track,PLAYING,OVERDUBBING',
            '#CAP,30535781,MI,U,144,8,31,98',
            '[30.537] [INFO] [TRACK] Reentry released slot=3 generation=1',
            '#CAP,30634265,MI,U,128,8,31,0',
            '#CAP,31980808,ST,Track,OVERDUBBING,OVERDUB_STOPPING',
            '#CAP,31980823,ST,Track,OVERDUB_STOPPING,PLAYING',
        ]
        for order in (lines, list(reversed(lines))):
            self.assertEqual(31980808, self.stop(order))

    def test_generic_release_alone_cannot_prove_a_stop(self):
        self.assertIsNone(self.stop([
            '#CAP,1000000,ST,Track,PLAYING,OVERDUBBING',
            '[2.000] Reentry released slot=3 generation=1',
        ]))

    def test_typed_state_stop_wins_over_unqualified_reentry_window(self):
        self.assertEqual(3000000, self.stop([
            '#CAP,1000000,ST,Track,PLAYING,OVERDUBBING',
            '[2.000] Reentry window open slot=3',
            '#CAP,3000000,ST,Track,OVERDUBBING,OVERDUB_STOPPING',
        ]))

    def test_typed_odub_stop_wins_when_state_stop_is_shed(self):
        self.assertEqual(3000000, self.stop([
            '#CAP,1000000,ST,Track,PLAYING,OVERDUBBING',
            '[2.000] Reentry window open slot=3',
            '#CAP,3000000,ODUB,stop,enter,3',
        ]))

    def test_explicit_human_stop_survives_without_state_stop(self):
        self.assertEqual(3000000, self.stop([
            '[1.000] Overdubbing started @ tick 0',
            '[2.000] Reentry released slot=3 generation=1',
            '[3.000] Overdubbing stopped @ tick 768',
        ]))

    def test_reentry_window_remains_a_missing_typed_stop_fallback(self):
        self.assertEqual(3000000, self.stop([
            '#CAP,1000000,ST,Track,PLAYING,OVERDUBBING',
            '[3.000] Reentry window open slot=3',
        ]))

    def test_predecessor_fallback_does_not_create_stale_stop_ambiguity(self):
        phase = extract_phase_sessions([
            '[0.500] Reentry window open slot=3',
            '#CAP,1000000,ST,Track,PLAYING,OVERDUBBING',
            '#CAP,3000000,ST,Track,OVERDUBBING,OVERDUB_STOPPING',
        ])
        self.assertEqual((), phase.ambiguities)

    def test_missing_stop_cannot_steal_the_next_human_interval_stop(self):
        from hitl.contract import PhaseAmbiguity
        for first in ('[1.000] Overdubbing started @ tick 0',
                      '#CAP,1000000,ST,Track,PLAYING,OVERDUBBING'):
            with self.subTest(first=first):
                phase = extract_phase_sessions([
                    first,
                    '[3.000] Overdubbing started @ tick 768',
                    '[4.000] Overdubbing stopped @ tick 1536',
                ])
                self.assertIn(PhaseAmbiguity.MISSING_STOP, phase.ambiguities)
                self.assertEqual((3000000, 4000000),
                                 (phase.overdub.start_ts, phase.overdub.stop_ts))


class BaselinePersistenceCleanupTests(unittest.TestCase):
    def cleanup(self, *, idle=True, outcome='ok', leg_error=False):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        log = Path(temp.name) / 'session.log'
        log.write_text('#CAP,100,PERS,request,0,1000,1000,ok\n'
                       '#CAP,200,PERS,work,0,1000,1000,LoopPersist,slot:7:4,save,ok\n')
        events = []
        class Port:
            closed = False
            def send(self, message):
                self_test.assertFalse(self.closed)
                events.append(message.type)
        port = Port()
        self_test = self
        def record_drain(send):
            send()
            events.append('record-drained')
            return {'complete': True, 'reason': 'Record rows drained'}
        session = SimpleNamespace(session_log=log, drain_record_evidence=record_drain)
        def observe_idle(collector, **kwargs):
            events.append('persistence-observed')
            if idle and outcome is not None:
                with log.open('a') as f:
                    f.write(f'#CAP,900,PERS,result,50,1000,1000,{outcome}\n')
            return idle
        result = {}
        with patch('hitl.managed_capture.active_managed_capture', return_value=session), \
             patch('hitl.device_shutdown._wait_for_verified_idle', side_effect=observe_idle), \
             patch('hitl.device_shutdown.DeviceSession._capture_heap_observation', return_value=[]), \
             patch('hitl.device_shutdown.mark_incomplete') as latch:
            if leg_error:
                with self.assertRaisesRegex(RuntimeError, 'original leg'):
                    with _managed_record_evidence_cleanup(port, result):
                        raise RuntimeError('original leg')
            else:
                with _managed_record_evidence_cleanup(port, result):
                    events.append('body-finished')
            port.closed = True
            events.append('port-closed')
            summary = _summarize_persistence_handoff(
                _extract_persistence_rows(log.read_text().splitlines()), record_stop_ts=50,
                lines=log.read_text().splitlines())
            return result, summary, events, latch.call_count

    def test_delayed_result_is_available_before_port_close_and_report(self):
        result, summary, events, latches = self.cleanup()
        self.assertTrue(summary['result_ok'], summary)
        self.assertEqual(2, result['persistence_shutdown']['drains'])
        self.assertEqual(3, events.count('stop'))  # Record retrieval plus two canonical cycles
        self.assertEqual(2, events.count('persistence-observed'))
        self.assertEqual('port-closed', events[-1])
        self.assertEqual(0, latches)

    def test_incomplete_persistence_drain_remains_incomplete_and_latched(self):
        result, summary, events, latches = self.cleanup(idle=False)
        self.assertFalse(result['complete'])
        self.assertFalse(result['persistence_shutdown']['ok'])
        self.assertFalse(summary['result_ok'])
        self.assertEqual(1, latches)

    def test_failed_result_is_not_converted_to_success_by_idle(self):
        result, summary, events, latches = self.cleanup(outcome='failed')
        self.assertFalse(summary['result_ok'])
        self.assertEqual('failed', summary['result']['outcome'])

    def test_missing_result_is_not_invented_by_idle(self):
        result, summary, events, latches = self.cleanup(outcome=None)
        self.assertFalse(summary['result_ok'])
        self.assertEqual('result', summary['first_missing_stage'])

    def test_original_leg_exception_survives_persistence_cleanup(self):
        result, summary, events, latches = self.cleanup(leg_error=True)
        self.assertTrue(summary['result_ok'])
        self.assertTrue(result['persistence_shutdown']['ok'])


if __name__ == '__main__':
    unittest.main()
