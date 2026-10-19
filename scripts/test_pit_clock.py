"""Host-only protocol, fault and qualification tests; never opens hardware."""
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from hitl.scenarios import pit_clock as pit


def sample(second=0, *, hz=24_000_000, **updates):
    raw = 240_000_000 + second * hz
    values = dict(id=second, status=2, era=1, rawTicks=raw, beforeUs=raw // 24,
                  widthUs=0, musicalTick=0, epoch=1, readCycles=120, cpuHz=600_000_000,
                  configBits=0x27f | (128 if raw >= 1 << 32 else 0), transport=0, source=0,
                  s_ns=1_000_000_000 + second * 1_000_000_000,
                  r_ns=1_001_000_000 + second * 1_000_000_000)
    values.update(updates)
    return values


def row(values=None):
    values = values or sample()
    return '#CAP,123,DIAG,pit_obs,' + ','.join(str(values[key]) for key in pit.FIELDS)


class PitProtocolTests(unittest.TestCase):
    def test_exact_positional_uint64_contract(self):
        values = sample(rawTicks=(1 << 64) - 1, beforeUs=(1 << 64) - 1)
        self.assertEqual(pit.parse_observation([row(values)], 0)['rawTicks'], (1 << 64) - 1)

    def test_missing_duplicate_wrong_id_and_malformed(self):
        for rows in ([], [row(), row()], [row(sample(id=1))], [row() + ',0'],
                     [row().replace(',123,', ',-1,')], [row(sample(rawTicks=1 << 64))],
                     [row(sample(transport=2))], [row(sample(id=-1))],
                     [row(sample(cpuHz='x'))]):
            with self.subTest(rows=rows), self.assertRaises(RuntimeError):
                pit.parse_observation(rows, 0)

    def test_managed_bracket_order_and_completion(self):
        events = []
        capture = Mock()
        def send(command, **kwargs):
            events.append('send')
            self.assertEqual(command, '!PIT_OBSERVE 0')
            self.assertNotEqual(kwargs['nonce'], kwargs['end_nonce'])
            return True, 'queued'
        capture.send_command.side_effect = send
        capture.fenced_response.side_effect = lambda *a, **k: events.append('fence') or [row()]
        def now():
            events.append('now')
            return 100 if len(events) == 1 else 200
        result = pit.observe(capture, 0, now=now)
        self.assertEqual(events, ['now', 'send', 'fence', 'now'])
        self.assertEqual((result['s_ns'], result['r_ns']), (100, 200))
        capture.complete_command.assert_called_once_with(result['nonce'])
        capture.close.assert_not_called()

    def test_refusal_timeout_exception_and_duplicate_preserved(self):
        for accepted, response in ((False, []), (True, None), (True, RuntimeError('fault')),
                                   (True, [row(), row()])):
            capture = Mock()
            capture.send_command.return_value = accepted, 'refused'
            if isinstance(response, Exception):
                capture.fenced_response.side_effect = response
            else:
                capture.fenced_response.return_value = response
            result = pit.observe(capture, 0, now=lambda: 100)
            self.assertIn('error', result)
            capture.complete_command.assert_called_once()
            capture.close.assert_not_called()

    def test_uint32_request_refusal_before_send(self):
        capture = Mock()
        with self.assertRaises(RuntimeError):
            pit.observe(capture, 1 << 32)
        capture.send_command.assert_not_called()

    def test_ring_delayed_row_requires_both_fences_and_matching_id(self):
        start, end = 'a' * 32, 'b' * 32
        # Real fence format is pinned by the production capture-ring sequence test.
        fences = [f'#CAP,1,TRACE_FENCE,{token}' for token in (start, end)]
        lines = [*fences, row()]
        self.assertEqual(pit.delayed_observation_rows(lines, start, end, 0), [row()])
        self.assertIsNone(pit.delayed_observation_rows(lines[1:], start, end, 0))
        self.assertIsNone(pit.delayed_observation_rows(lines, start, end, 1))
        self.assertIsNone(pit.delayed_observation_rows([fences[0], *lines], start, end, 0))
        with self.assertRaises(RuntimeError):
            pit.parse_observation(pit.delayed_observation_rows([*lines, row()], start, end, 0), 0)

    def test_delayed_row_closes_host_bracket_after_delivery(self):
        capture = Mock()
        capture.send_command.return_value = True, 'queued'
        capture.fenced_response.return_value = [] # end arrives before ring row
        capture.session_log.is_file.return_value = True
        def read(**kwargs):
            request = capture.send_command.call_args.kwargs
            return '\n'.join([f"#CAP,1,TRACE_FENCE,{request['nonce']}",
                              f"#CAP,2,TRACE_FENCE,{request['end_nonce']}", row()])
        capture.session_log.read_text.side_effect = read
        ticks = iter((100, 300))
        result = pit.observe(capture, 0, now=lambda: next(ticks))
        self.assertNotIn('error', result)
        self.assertEqual(result['r_ns'], 300)
        self.assertEqual(result['response_rows'], [row()])
        capture.complete_command.assert_called_once()

    def test_wrong_delayed_id_times_out_without_closing_host_bracket(self):
        capture = Mock()
        capture.send_command.return_value = True, 'queued'
        capture.fenced_response.return_value = []
        capture.session_log.is_file.return_value = True
        capture.session_log.read_text.return_value = row(sample(id=1))
        with patch.object(pit.time, 'monotonic', side_effect=(0, 3)):
            result = pit.observe(capture, 0, now=lambda: 100)
        self.assertIn('pit_obs_delayed_row_missing', result['error'])
        self.assertNotIn('r_ns', result)
        capture.complete_command.assert_called_once()


class PitOracleTests(unittest.TestCase):
    def test_startup_fault_matrix(self):
        for updates in (dict(status=1), dict(era=0), dict(configBits=0x27e),
                        dict(configBits=0x37f), dict(readCycles=0), dict(cpuHz=0),
                        dict(transport=1), dict(beforeUs=20_000_000),
                        dict(rawTicks=24_000_000 * 60, beforeUs=60_000_000),
                        dict(s_ns=2_000_000_000), dict(error='missing')):
            with self.subTest(updates=updates), self.assertRaises(RuntimeError):
                pit.validate_sample(sample(**updates))

    def test_submicrosecond_remainder_matches_production_conversion(self):
        pit.validate_sample(sample(rawTicks=240_000_023))

    def test_era_epoch_transport_source_and_stopped_tick_changes(self):
        first = sample()
        for updates in (dict(era=2), dict(epoch=2), dict(musicalTick=1),
                        dict(source=1), dict(cpuHz=500_000_000), dict(transport=1)):
            with self.subTest(updates=updates), self.assertRaises(RuntimeError):
                pit.validate_sample(sample(1, **updates), first)

    def test_monotonic_ticks_and_host_brackets(self):
        for updates in (dict(rawTicks=240_000_000, beforeUs=10_000_000),
                        dict(rawTicks=239_999_976, beforeUs=9_999_999),
                        dict(s_ns=1_001_000_000)):
            with self.subTest(updates=updates), self.assertRaises(RuntimeError):
                pit.validate_sample(sample(1, **updates), sample())

    def test_rate_interval_exact_enclosing_formula(self):
        first, last = sample(), sample(3)
        interval = pit.rate_interval([first, last])
        self.assertAlmostEqual(interval[0], 72_000_000 / 3.001)
        self.assertAlmostEqual(interval[1], 72_000_000 / 2.999)
        self.assertLess(interval[0], 24_000_000)
        self.assertGreater(interval[1], 24_000_000)

    def test_rate_nonpositive_denominator_refused(self):
        with self.assertRaises(RuntimeError):
            pit.rate_interval([sample(), sample(1, s_ns=1_001_000_000)])

    def test_gross_rate_acceptance_and_outside_rejection(self):
        for hz, verdict in ((24_000_000, 'SMOKE_PASS'), (23_900_000, 'SMOKE_PASS'),
                            (24_100_000, 'SMOKE_PASS'), (25_000_000, 'FAIL')):
            result = pit.evaluate([sample(i, hz=hz) for i in range(4)], 3)
            self.assertEqual(result['verdict'], verdict)

    def test_full_natural_first_low_carry(self):
        result = pit.evaluate([sample(i) for i in range(211)], 210)
        self.assertTrue(result['ok'], result['issues'])
        self.assertEqual(result['verdict'], 'PASS')
        self.assertEqual(result['latching'], 'PASS')
        self.assertTrue(result['natural_first_low_carry_observed'])

    def test_missing_carry_low_flag_or_samples_fails_full(self):
        samples = [sample(i) for i in range(211)]
        samples[170]['configBits'] &= ~128
        result = pit.evaluate(samples, 210)
        self.assertFalse(result['ok'])
        self.assertIn('lowTIF_missing_after_carry', result['issues'])
        result = pit.evaluate([sample(i) for i in range(4)], 210)
        self.assertIn('natural_first_low_carry_missing', result['issues'])
        self.assertIn('full_observation_duration_missing', result['issues'])
        self.assertIn('sample_count_missing_or_invalid_duration', result['issues'])

    def test_smoke_is_not_qualification_and_latency_has_no_threshold(self):
        result = pit.evaluate([sample(i, readCycles=600_000_000) for i in range(4)], 3)
        self.assertEqual(result['verdict'], 'SMOKE_PASS')
        self.assertEqual(result['latching'], 'SMOKE_ONLY')
        self.assertEqual(result['latency'], 'OBSERVED_NO_WCET')
        self.assertEqual(result['observed_read_latency_ns'],
                         dict(min=1e9, median=1e9, p95=1e9, max=1e9, samples=4))
        self.assertIsNone(pit.ORACLE['latency_threshold'])
        self.assertEqual(result['stopped_point_correlation'], 'PASS')

    def test_stopped_overlay_preflight_refusals(self):
        capture = Mock()
        for state, overlay in ((None, None), (dict(state='PLAYING', transport=1), dict(open=0, phase=0)),
                               (dict(state='STOPPED', transport=0), dict(open=1, phase=0)),
                               (dict(state='EMPTY', transport=0), dict(open=0, phase=1))):
            capture.selected_track_status.return_value = state
            capture.overlay_status.return_value = overlay
            with self.assertRaises(RuntimeError):
                pit.preflight(capture)
        capture.selected_track_status.return_value = dict(state='STOPPED', transport=0)
        capture.overlay_status.return_value = dict(open=0, phase=0)
        self.assertIn('selected_track_status', pit.preflight(capture))


class PitRunnerTests(unittest.TestCase):
    def test_discoverable_temporary_stimulus_and_outer_owned_preset(self):
        from hitl.registry import PRESET_SCENARIOS, get_registry
        from hitl.stimuli.registry import list_stimuli, stimulus_for_scenario, format_stimulus_list_lines
        spec = stimulus_for_scenario('pit_clock')
        self.assertEqual(spec.kind, 'temporary')
        self.assertIsNone(spec.authorization)
        self.assertNotIn(spec, list_stimuli())  # canonical YAML export remains unchanged
        self.assertTrue(any(spec.fingerprint_key() in line for line in format_stimulus_list_lines(include_temporary=True)))
        self.assertEqual(PRESET_SCENARIOS['pit_clock'], ('pit_clock',))
        self.assertFalse(get_registry()['pit_clock'].owns_capture)

    def test_runner_refusals_write_report_without_device_action(self):
        for mode, duration in (('auto', '3'), ('force', '241'), ('force', 'nan')):
            with tempfile.TemporaryDirectory(dir=os.environ.get('PI_SCRATCH_DIR')) as directory:
                config = SimpleNamespace(out_dir=Path(directory), boot_reset=mode)
                with patch('hitl.layered_cli.config_from_runner_args', return_value=config), \
                     patch('hitl.managed_capture.active_managed_capture') as active:
                    rc = pit.run_pit_clock(SimpleNamespace(legacy_args=['--pit-duration-s', duration]))
                self.assertEqual(rc, 2)
                active.assert_not_called()
                report = json.loads((Path(directory) / 'pit-clock.json').read_text())
                self.assertEqual(report['verdict'], 'FAIL')
                self.assertEqual(report['oracle_manifest']['rate_acceptance_hz'], [23_900_000, 24_100_000])

    def test_runner_stops_first_invalid_preserves_sample_and_leaves_serial_owner(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('PI_SCRATCH_DIR')) as directory:
            config = SimpleNamespace(out_dir=Path(directory), boot_reset='force', serial_port=None,
                                     midi_out_name='test', midi_in_name='test')
            capture, session, collector = Mock(), Mock(), Mock()
            capture.session_log = Path(directory) / 'session.log'
            capture.selected_track_status.return_value = dict(state='STOPPED', transport=0)
            capture.overlay_status.return_value = dict(open=0, phase=0)
            with patch('hitl.layered_cli.config_from_runner_args', return_value=config), \
                 patch('hitl.managed_capture.active_managed_capture', return_value=capture), \
                 patch('hitl.serial_follow.ExternalSerialFollowCollector', return_value=collector), \
                 patch('hitl.session.open_midi_session', return_value=session), \
                 patch.object(pit, 'observe', return_value=dict(id=0, s_ns=1, error='marker_missing')) as observe, \
                 patch.object(pit.time, 'sleep'):
                rc = pit.run_pit_clock(SimpleNamespace(legacy_args=['--pit-duration-s', '3']))
            self.assertEqual(rc, 2)
            observe.assert_called_once()
            session.close.assert_called_once()
            capture.close.assert_not_called()
            capture.send_command.assert_not_called()
            report = json.loads((Path(directory) / 'pit-clock.json').read_text())
            self.assertEqual(report['samples'][0]['error'], 'marker_missing')
            self.assertEqual(report['verdict'], 'FAIL')


if __name__ == '__main__':
    unittest.main()
