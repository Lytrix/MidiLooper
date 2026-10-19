"""Offline moving PIT contract and lifecycle tests; no hardware opened."""
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from hitl.scenarios import pit_motion as pit


def sample(index=0, **changes):
    raw = 240_000_000 + index * 24_000_000
    s = dict(id=index, status=2, era=2, rawTicks=raw, beforeUs=raw // 24,
             widthUs=0, musicalTick=100 + index * 1000, epoch=3,
             periodUs=1000, guardBits=9, configBits=0x27f, transport=1, source=0)
    s.update(changes)
    return s


def row(s=None):
    s = sample() if s is None else s
    return '#CAP,123,DIAG,pit_motion,' + ','.join(str(s[k]) for k in pit.FIELDS)


class ProtocolTests(unittest.TestCase):
    def test_exact_contract_uint64_and_uint32(self):
        s = sample(rawTicks=(1 << 64) - 1, periodUs=(1 << 32) - 1)
        self.assertEqual(len(row(s).split(',')), 17)
        self.assertEqual(pit.parse_observation([row(s)], 0)['rawTicks'], s['rawTicks'])

    def test_missing_duplicate_wrong_id_range_shape(self):
        for rows in ([], [row(), row()], [row(sample(id=7))], [row() + ',1'],
                     [row(sample(rawTicks=1 << 64))], [row(sample(periodUs=1 << 32))],
                     [row(sample(guardBits=-1))], [row(sample(source=2))],
                     [row().replace(',123,', ',4294967296,')], [row(sample(epoch='x'))]):
            with self.subTest(rows=rows), self.assertRaises(RuntimeError):
                pit.parse_observation(rows, 0)

    def test_delayed_rows_require_both_exact_unique_ordered_fences(self):
        a, b = 'a' * 32, 'b' * 32
        start, end = f'#CAP,1,TRACE_FENCE,{a}', f'#CAP,2,TRACE_FENCE,{b}'
        from hitl.serial.protocol import fence_token
        self.assertEqual(fence_token(start), a)
        self.assertEqual(pit.delayed_observation_rows([start, end, row()], a, b, 0), [row()])
        for lines in ([start, row()], [end, row()], [end, start, row()],
                      [start, start, end, row()], [start + 'x', end, row()]):
            self.assertIsNone(pit.delayed_observation_rows(lines, a, b, 0))
        rows = pit.delayed_observation_rows([start, end, row(), row()], a, b, 0)
        with self.assertRaises(RuntimeError):
            pit.parse_observation(rows, 0)

    def test_observe_completion_on_success_refusal_and_missing_fence(self):
        for accepted, response in ((True, [row()]), (False, []), (True, None),
                                   (True, [row(sample(id=7))]), (True, [row(), row()])):
            c = Mock(session_log=None)
            c.send_command.return_value = accepted, 'detail'
            c.fenced_response.return_value = response
            s = pit.observe(c, 0)
            self.assertEqual('error' not in s, accepted and response == [row()])
            c.complete_command.assert_called_once_with(s['nonce'])
            c.send_command.assert_called_once_with('!PIT_MOTION 0', nonce=s['nonce'], end_nonce=s['end_nonce'])
            c.close.assert_not_called()
        for request in (-1, 1 << 32, True):
            with self.assertRaises(RuntimeError):
                pit.observe(Mock(), request)

    def test_delayed_observe_reads_after_end(self):
        with tempfile.TemporaryDirectory() as d:
            c = Mock(session_log=Path(d) / 'session.log')
            def send(command, nonce, end_nonce):
                c.session_log.write_text(f'#CAP,1,TRACE_FENCE,{nonce}\n#CAP,2,TRACE_FENCE,{end_nonce}\n{row()}\n')
                return True, 'ok'
            c.send_command.side_effect = send
            c.fenced_response.return_value = []
            self.assertNotIn('error', pit.observe(c, 0))


    def test_immediate_row_plus_post_fence_duplicate_fails(self):
        with tempfile.TemporaryDirectory() as d:
            c = Mock(session_log=Path(d) / 'session.log')
            def send(command, nonce, end_nonce):
                c.session_log.write_text(f'#CAP,1,TRACE_FENCE,{nonce}\n{row()}\n#CAP,2,TRACE_FENCE,{end_nonce}\n{row()}\n')
                return True, 'ok'
            c.send_command.side_effect = send
            c.fenced_response.return_value = [row()]
            self.assertIn('missing_or_duplicate', pit.observe(c, 0)['error'])

class EvaluationTests(unittest.TestCase):
    def test_stable_internal_observation_residual_not_threshold(self):
        result = pit.evaluate([sample(i) for i in range(5)], source=0)
        self.assertTrue(result['ok'])
        self.assertEqual(result['internal_residual_us'], dict(count=4, min=0, median=0, max=0))
        samples = [sample(i, periodUs=900 if i >= 2 else 1000) for i in range(5)]
        self.assertTrue(pit.evaluate(samples, source=0)['ok'])
        self.assertEqual(pit.evaluate(samples, source=0)['internal_residual_us']['count'], 3)

    def test_epoch_source_era_guard_and_monotonic_refusals(self):
        for changes in (dict(epoch=0), dict(epoch=4), dict(source=1), dict(era=0),
                        dict(era=3), dict(status=1), dict(guardBits=8), dict(guardBits=1),
                        dict(transport=0), dict(rawTicks=240_000_000), dict(musicalTick=100),
                        dict(beforeUs=0), dict(configBits=0), dict(periodUs=0)):
            with self.subTest(changes=changes), self.assertRaises(RuntimeError):
                pit.validate_sample(sample(1, **changes), sample(), source=0)

    def test_stopped_guard_all_bits_and_external(self):
        pit.validate_sample(sample(transport=0, guardBits=15), stopped=True)
        for bits in (0, 1, 9, 11, 13, 14):
            with self.assertRaises(RuntimeError):
                pit.validate_sample(sample(transport=0, guardBits=bits), stopped=True)
        self.assertTrue(pit.evaluate([sample(i, source=1) for i in range(6)], source=1, points=6)['ok'])
        self.assertFalse(pit.evaluate([sample()], source=0)['ok'])


class PacingTests(unittest.TestCase):
    def test_units_failed_send_and_limit_before_side_effect(self):
        port = Mock()
        times = iter((0, 10, 20_000_000, 20_000_100))
        p = pit.ClockSendProbe(port, limit=2, now=lambda: next(times))
        clock = SimpleNamespace(type='clock')
        p.send(clock)
        port.send.side_effect = OSError('send failed')
        with self.assertRaises(OSError):
            p.send(clock)
        self.assertEqual(p.attempts[1], dict(before_ns=20_000_000, after_ns=20_000_100, ok=False))
        with self.assertRaisesRegex(RuntimeError, 'before_send'):
            p.send(clock)
        self.assertEqual(port.send.call_count, 2)
        self.assertEqual(len(p.attempts), 2)
        self.assertEqual(p.report(120)['interval_ms']['median'], 20)
        self.assertAlmostEqual(p.report(300)['nominal_interval_ms'], 8.3333333333)
        port.send.side_effect = None
        p.send(SimpleNamespace(type='stop'))
        self.assertEqual(len(p.attempts), 2)
        for limit in (0, 8001):
            with self.assertRaises(ValueError):
                pit.ClockSendProbe(port, limit=limit)

    def test_registration_and_allowlist(self):
        from capture_session import is_allowed_hitl_command
        from hitl.registry import resolve_scenarios, get_registry
        from hitl.stimuli.registry import stimulus_for_scenario, list_stimuli, format_stimulus_list_lines
        self.assertEqual(resolve_scenarios(preset='pit_motion', scenario_ids=None), ['pit_motion'])
        self.assertFalse(get_registry()['pit_motion'].owns_capture)
        spec = stimulus_for_scenario('pit_motion')
        self.assertEqual(spec.stimulus_id, 'STIM-PIT-MOTION-TEMP-01')
        self.assertIsNone(spec.authorization)
        self.assertNotIn(spec, list_stimuli())
        self.assertTrue(any(spec.stimulus_id in line for line in format_stimulus_list_lines(include_temporary=True)))
        for cmd in ('!PIT_MOTION 0', '!PIT_MOTION 4294967295'):
            self.assertTrue(is_allowed_hitl_command(cmd))
        for cmd in ('!PIT_MOTION -1', '!PIT_MOTION 4294967296', '!PIT_MOTION 1 2', '!PIT_MOTION 1\n'):
            self.assertFalse(is_allowed_hitl_command(cmd))


class LifecycleTests(unittest.TestCase):
    def run_fake(self, directory, *, points=5, fault=None, close_fault=False, stopped_guard=15):
        config = SimpleNamespace(out_dir=Path(directory), serial_port=None, midi_out_name='out', midi_in_name='in', press_ms=30)
        capture, session, collector, master = Mock(), Mock(), Mock(), Mock()
        capture.session_log = Path(directory) / 'session.log'
        master.pump_error.return_value = None
        master.pulses_sent.return_value = 99
        session.close.side_effect = OSError('close fault') if close_fault else None
        state = dict(moving=False, source=0, index=0)
        def press(action):
            state['moving'] = not state['moving']
        def start(**kw):
            state.update(moving=True, source=1)
        def stop():
            state['moving'] = False
        master.start.side_effect, master.stop.side_effect = start, stop
        def observe(c, request, **kw):
            state['index'] += 1
            s = sample(state['index'], id=request, source=state['source'], transport=int(state['moving']), guardBits=9 if state['moving'] else stopped_guard)
            if fault and fault(state):
                s['error'] = 'injected observation fault'
            return s
        with patch('hitl.layered_cli.config_from_runner_args', return_value=config), \
             patch('hitl.managed_capture.active_managed_capture', return_value=capture), \
             patch('hitl.serial_follow.ExternalSerialFollowCollector', return_value=collector), \
             patch('hitl.session.open_midi_session', return_value=session), \
             patch('hitl.actions.buttons.press_transport', side_effect=press) as press_mock, \
             patch('hitl.midi_clock.MidiClockMaster', return_value=master), \
             patch.object(pit, 'observe', side_effect=observe), \
             patch.object(pit, 'preflight', return_value={}), patch.object(pit.time, 'sleep'):
            rc = pit.run_pit_motion(SimpleNamespace(legacy_args=['--pit-motion-points', str(points)]))
        report = json.loads((Path(directory) / 'pit-motion.json').read_text())
        return rc, report, session, capture, master, press_mock

    def test_three_legs_and_cleanup(self):
        with tempfile.TemporaryDirectory() as d:
            rc, r, session, capture, master, press = self.run_fake(d)
        self.assertEqual(rc, 0)
        self.assertEqual([(x['source'], x['bpm']) for x in r['legs']], [(0, None), (1, 120), (1, 300)])
        self.assertEqual(master.start.call_count, 2)
        self.assertEqual(master.stop.call_count, 2)
        self.assertEqual(press.call_count, 2)
        session.close.assert_called_once()
        capture.close.assert_not_called()
        self.assertIn('BLOCKED', r['limitations']['independent_calibration'])

    def test_failure_preserved_and_no_blind_toggle(self):
        for external in (False, True):
            with tempfile.TemporaryDirectory() as d:
                rc, r, session, capture, master, press = self.run_fake(d, fault=lambda s: s['moving'] and s['source'] == int(external))
            self.assertEqual(rc, 2)
            self.assertTrue(r['issues'])
            self.assertEqual(press.call_count, 2 if external else 1)
            if external:
                master.stop.assert_called_once()
            session.close.assert_called_once()
            capture.close.assert_not_called()

    def test_idle_timeout_refuses_all_gestures(self):
        import itertools
        with tempfile.TemporaryDirectory() as d, patch.object(pit.time, 'monotonic', side_effect=itertools.count(0, 1)):
            rc, r, session, capture, master, press = self.run_fake(d, stopped_guard=11)
        self.assertEqual(rc, 2)
        self.assertIn('stopped_storage_idle_timeout', r['issues'][-1])
        press.assert_not_called()
        master.start.assert_not_called()
        session.close.assert_called_once()

    def test_global_60_second_limit_refuses_start(self):
        import itertools
        with tempfile.TemporaryDirectory() as d, patch.object(pit.time, 'monotonic', side_effect=itertools.count(0, 61)):
            rc, r, session, capture, master, press = self.run_fake(d)
        self.assertEqual(rc, 2)
        self.assertIn('maximum_60s_exceeded', r['issues'][-1])
        press.assert_not_called()
        master.start.assert_not_called()

    def test_limits_and_close_failure(self):
        with tempfile.TemporaryDirectory() as d:
            rc, r, session, capture, master, press = self.run_fake(d, points=7)
            self.assertEqual(rc, 2)
            press.assert_not_called()
            session.close.assert_not_called()
        with tempfile.TemporaryDirectory() as d:
            rc, r, *_ = self.run_fake(d, close_fault=True)
            self.assertEqual(rc, 2)
            self.assertIn('close fault', r['issues'][-1])


if __name__ == '__main__':
    unittest.main()
