"""Offline ingress-cost contract and real scenario-policy tests; no device access."""
import argparse
from contextlib import ExitStack
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from hitl.scenarios import ingress_cost as cost


def snapshot(n=0, request=0):
    result = []
    for route in range(3):
        for lane in range(4):
            count = n * (3 if lane == 2 else 1) if route == 0 else 0
            result.append(dict(id=request, route=route, lane=lane, cpuHz=600000000,
                               guardBits=15, accepted=count, rejected=0,
                               minCycles=10 if count else 4294967295,
                               maxCycles=20 if count else 0, sumCycles=count * 15,
                               saturated=0, contractBits=7))
    return result


def rows(stats=None):
    return ['#CAP,123,DIAG,ingress_cost,' + ','.join(str(s[k]) for k in cost.FIELDS)
            for s in (snapshot() if stats is None else stats)]


class ParserTests(unittest.TestCase):
    def test_exact_schema_all_routes_lanes_sorted(self):
        self.assertEqual(cost.parse_snapshot(reversed(rows()), 0), snapshot())
        self.assertEqual(len(rows()[0].split(',')), 16)

    def test_large_sum_unsigned_boundaries(self):
        s = snapshot(1)
        s[0].update(accepted=4294967295, minCycles=4294967295, maxCycles=4294967295,
                    sumCycles=4294967295 ** 2, rejected=4294967295)
        self.assertEqual(cost.parse_snapshot(rows(s), 0)[0]['sumCycles'], 4294967295 ** 2)

    def test_missing_duplicate_wrong_id_and_shape(self):
        for r in ([], rows()[:-1], rows() + rows()[:1], rows()[:-1] + rows()[:1],
                  [rows()[0] + ',0'] + rows()[1:],
                  [rows()[0].replace('DIAG,', 'OTHER,')] + rows()[1:],
                  rows(snapshot(request=1))):
            with self.subTest(r=r), self.assertRaises(RuntimeError):
                cost.parse_snapshot(r, 0)

    def test_unsigned_ranges_and_fixed_contract(self):
        invalid = dict(id=1 << 32, route=3, lane=4, cpuHz=599999999, guardBits=14,
                       accepted=1 << 32, rejected=-1, minCycles=1 << 32,
                       maxCycles=1 << 32, sumCycles=1 << 64, saturated=2, contractBits=3)
        for k, v in invalid.items():
            s = snapshot()
            s[0][k] = v
            with self.subTest(key=k), self.assertRaises(RuntimeError):
                cost.parse_snapshot(rows(s), 0)
        for value in ('+0', '-0', ' 0', '0 ', '1.0', '١', ''):
            s = snapshot()
            s[0]['rejected'] = value
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                cost.parse_snapshot(rows(s), 0)
        for r in ([rows()[0].replace(',123,', ',4294967296,')] + rows()[1:],
                  [rows()[0] + ' '] + rows()[1:]):
            with self.assertRaises(RuntimeError):
                cost.parse_snapshot(r, 0)

    def test_empty_sentinel_and_nonempty_sum_extrema(self):
        for changes in (dict(minCycles=0), dict(maxCycles=1), dict(sumCycles=1),
                        dict(accepted=1, minCycles=20, maxCycles=10, sumCycles=15),
                        dict(accepted=2, minCycles=10, maxCycles=20, sumCycles=19),
                        dict(accepted=2, minCycles=10, maxCycles=20, sumCycles=41)):
            s = snapshot()
            s[0].update(changes)
            with self.subTest(changes=changes), self.assertRaises(RuntimeError):
                cost.parse_snapshot(rows(s), 0)

    def test_saturation_never_verdict_qualified(self):
        s = snapshot(1)
        s[7]['saturated'] = 1
        with self.assertRaisesRegex(RuntimeError, 'saturated'):
            cost.parse_snapshot(rows(s), 0)

    def test_request_ids_strict_uint32(self):
        for request in (-1, 1 << 32, True, 1.0):
            with self.subTest(request=request), self.assertRaises(RuntimeError):
                cost.observe(Mock(), request)


class FenceTests(unittest.TestCase):
    def test_exact_unique_ordered_fences_and_post_end_rows(self):
        a, b = 'a' * 32, 'b' * 32
        start, end = f'#CAP,1,TRACE_FENCE,{a}', f'#CAP,2,TRACE_FENCE,{b}'
        self.assertEqual(cost.delayed_snapshot_rows([start, end, *rows()], a, b), rows())
        for lines in ([start, *rows()], [end, start, *rows()], [start, start, end, *rows()],
                      [start, end, end, *rows()], [start + 'x', end, *rows()]):
            self.assertIsNone(cost.delayed_snapshot_rows(lines, a, b))

    def observe_log(self, *, extra=(), immediate=True, truncate=False, duplicate_fence=False):
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / 'session.log'
            # Historic bytes are not read: request offset starts at EOF.
            log.write_text('old unrelated\n' * 30000)
            c = Mock(session_log=log)
            def send(command, nonce, end_nonce):
                self.assertEqual(command, '!INGRESS_COST 0')
                with log.open('a') as f:
                    f.write(f'#CAP,1,TRACE_FENCE,{nonce}\n')
                    if duplicate_fence:
                        f.write(f'#CAP,1,TRACE_FENCE,{nonce}\n')
                    if immediate:
                        f.write('\n'.join(rows()) + '\n')
                    if not truncate:
                        f.write(f'#CAP,2,TRACE_FENCE,{end_nonce}\n')
                    if not immediate:
                        f.write('\n'.join(rows()) + '\n')
                    f.write('\n'.join(extra) + '\n')
                return True, 'ok'
            c.send_command.side_effect = send
            c.fenced_response.return_value = rows() if immediate else []
            result = cost.observe(c, 0, timeout_s=.02)
            c.complete_command.assert_called_once_with(result['nonce'])
            c.close.assert_not_called()
            return result

    def test_delayed_ring_rows_after_end_fence(self):
        self.assertNotIn('error', self.observe_log(immediate=False))

    def test_immediate_rows_still_scan_available_tail_duplicates(self):
        self.assertIn('missing_or_duplicate', self.observe_log(extra=rows()[:1])['error'])

    def test_wrong_id_in_available_tail_refuses(self):
        self.assertIn('missing_or_duplicate', self.observe_log(extra=rows(snapshot(request=9))[:1])['error'])

    def test_missing_or_duplicate_log_fences_fail_even_if_immediate_rows(self):
        for options in (dict(truncate=True), dict(duplicate_fence=True)):
            self.assertIn('error', self.observe_log(**options))

    def test_refusal_missing_fence_and_completion(self):
        for accepted, response in ((False, []), (True, None), (True, rows(snapshot(request=1)))):
            c = Mock(session_log=None)
            c.send_command.return_value = accepted, 'detail'
            c.fenced_response.return_value = response
            result = cost.observe(c, 0)
            self.assertIn('error', result)
            c.complete_command.assert_called_once_with(result['nonce'])

    def test_bounded_request_tail_refuses(self):
        self.assertIn('request_tail_exceeds_bound', self.observe_log(extra=['x' * 262145])['error'])

    def test_ring_rows_arriving_on_later_poll(self):
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / 'session.log'
            c = Mock(session_log=log)
            def send(command, nonce, end_nonce):
                log.write_text(f'#CAP,1,TRACE_FENCE,{nonce}\n#CAP,2,TRACE_FENCE,{end_nonce}\n')
                return True, 'ok'
            c.send_command.side_effect = send
            c.fenced_response.return_value = []
            def deliver(_seconds):
                with log.open('a') as f:
                    f.write('\n'.join(rows()) + '\n')
            with patch.object(cost.time, 'sleep', side_effect=deliver) as pause:
                self.assertNotIn('error', cost.observe(c, 0))
                pause.assert_called_once()

    def test_partial_twelfth_row_waits_for_newline_and_remaining_bytes(self):
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / 'session.log'
            c = Mock(session_log=log)
            final = rows()[-1]
            cut = final.index(',ingress_cost,') + len(',ingress_cost,') + 3
            def send(command, nonce, end_nonce):
                log.write_text(f'#CAP,1,TRACE_FENCE,{nonce}\n#CAP,2,TRACE_FENCE,{end_nonce}\n'
                               + '\n'.join(rows()[:-1]) + '\n' + final[:cut])
                return True, 'ok'
            c.send_command.side_effect = send
            c.fenced_response.return_value = []
            def finish(_seconds):
                with log.open('a') as stream:
                    stream.write(final[cut:] + '\r\n')
            with patch.object(cost.time, 'sleep', side_effect=finish) as pause:
                result = cost.observe(c, 0)
            self.assertNotIn('error', result)
            pause.assert_called_once()
            self.assertEqual(len(result['rows']), 12)
            c.complete_command.assert_called_once_with(result['nonce'])

    def test_complete_malformed_twelfth_row_still_refuses_without_waiting(self):
        malformed = rows()[-1].split(',ingress_cost,')[0] + ',ingress_cost,0,2\n'
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / 'session.log'
            c = Mock(session_log=log)
            def send(command, nonce, end_nonce):
                log.write_text(f'#CAP,1,TRACE_FENCE,{nonce}\n#CAP,2,TRACE_FENCE,{end_nonce}\n'
                               + '\n'.join(rows()[:-1]) + '\n' + malformed)
                return True, 'ok'
            c.send_command.side_effect = send
            c.fenced_response.return_value = []
            with patch.object(cost.time, 'sleep') as pause:
                result = cost.observe(c, 0)
            self.assertIn('invalid_ingress_cost_contract', result['error'])
            pause.assert_not_called()
            c.complete_command.assert_called_once_with(result['nonce'])

    def test_sample_budget_covers_fences_and_delayed_rows_together(self):
        c = Mock(session_log=None)
        c.send_command.return_value = True, 'ok'
        clock = FakeTime()
        def response(*args, **kw):
            self.assertLessEqual(kw['timeout_s'], 2)
            clock.value += 2.01
            return rows()
        c.fenced_response.side_effect = response
        with patch.object(cost.time, 'monotonic', clock.monotonic):
            result = cost.observe(c, 0)
        self.assertIn('sample_deadline_exceeded', result['error'])
        c.complete_command.assert_called_once()


class DeltaTests(unittest.TestCase):
    def test_only_counts_sums_differenced_extrema_lifetime(self):
        a, b = snapshot(10), snapshot(12)
        b[0].update(minCycles=5, maxCycles=30)
        d = cost.evaluate_leg(a, b)['count_sum_deltas'][0]
        self.assertEqual((d['accepted'], d['sumCycles']), (2, 30))
        self.assertEqual((d['lifetime_minCycles'], d['lifetime_maxCycles']), (5, 30))
        self.assertNotIn('minCycles', d)

    def test_quiescent_ratios_and_nonzero_hook_required(self):
        for b in (snapshot(), snapshot(2)):
            if b[0]['accepted']:
                b[2]['accepted'] -= 1
                b[2]['sumCycles'] -= 15
            with self.assertRaisesRegex(RuntimeError, 'ratios'):
                cost.evaluate_leg(snapshot(), b)

    def test_new_rejection_fails(self):
        b = snapshot(2)
        b[0]['rejected'] = 1
        with self.assertRaisesRegex(RuntimeError, 'rejected'):
            cost.evaluate_leg(snapshot(), b)

    def test_monotonic_counts_sums_and_extrema(self):
        for change in (dict(accepted=1), dict(rejected=-1), dict(sumCycles=1),
                       dict(minCycles=11), dict(maxCycles=19)):
            b = snapshot(2)
            b[0].update(change)
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                cost.count_sum_deltas(snapshot(2), b)

    def test_delta_sum_checked_without_extrema_subtraction(self):
        a, b = snapshot(2), snapshot(3)
        b[0]['sumCycles'] = 59  # lifetime valid (30..60), new 29 > lifetime max20
        cost.parse_snapshot(rows(b), 0)
        with self.assertRaisesRegex(RuntimeError, 'delta_sum'):
            cost.count_sum_deltas(a, b)


class FakeTime:
    def __init__(self):
        self.value = 100.0
    def monotonic(self):
        return self.value
    def sleep(self, seconds):
        self.value += seconds


class Capture:
    """Transport seam only: production parsers, guards, ratios and scenario execute."""
    def __init__(self, path, clock, *, guard_changes=None, unstable=False, bad_ratio=False, saturated=False):
        self.session_log = path
        path.touch()
        self.clock, self.n, self.guard_index = clock, 0, 0
        self.guard_changes = guard_changes or {}
        self.unstable, self.bad_ratio, self.saturated = unstable, bad_ratio, saturated
        self.commands, self.cost_times = [], []
        self.completed, self.active, self.pending = [], False, False
        self.inflight = False
    def selected_track_status(self, **kw):
        return dict(transport=0, state='STOPPED')
    def overlay_status(self, **kw):
        return dict(open=0, phase=0)
    def send_command(self, command, nonce, end_nonce):
        self.commands.append(command)
        request = int(command.split()[1])
        if command.startswith('!INGRESS_COST '):
            if self.active:
                raise AssertionError('snapshot during pump')
            self.cost_times.append(self.clock.monotonic())
            s = snapshot(self.n, request)
            if self.unstable:
                self.n += 1
            if self.pending:
                # First after-pump snapshot can catch a final publication still in flight.
                s[2]['accepted'] -= 1
                s[2]['sumCycles'] -= 15
                self.pending = False
                self.inflight = True
            if self.bad_ratio and self.n:
                s[2]['accepted'] -= 1
                s[2]['sumCycles'] -= 15
            if self.saturated:
                s[0]['saturated'] = 1
            self.response = rows(s)
        else:
            if not command.startswith('!PIT_MOTION '):
                raise AssertionError(command)
            self.guard_index += 1
            raw = self.guard_index * 24000000
            s = dict(id=request, status=2, era=2, rawTicks=raw, beforeUs=raw // 24,
                     widthUs=0, musicalTick=0, epoch=1, periodUs=1000, guardBits=15,
                     configBits=0x27f, transport=0, source=int(bool(self.n)))
            s.update(self.guard_changes.get(self.guard_index, {}))
            self.response = ['#CAP,123,DIAG,pit_motion,' + ','.join(str(s[k]) for k in cost.pit_motion.FIELDS)]
        with self.session_log.open('a') as f:
            f.write(f'#CAP,1,TRACE_FENCE,{nonce}\n' + '\n'.join(self.response) +
                    f'\n#CAP,2,TRACE_FENCE,{end_nonce}\n')
        return True, 'ok'
    def fenced_response(self, *args, **kw):
        return self.response
    def complete_command(self, nonce):
        self.completed.append(nonce)


class ScenarioTests(unittest.TestCase):
    def exercise(self, *, options=(), guard_changes=None, unstable=False, bad_ratio=False,
                 pump_failure=False, saturated=False, no_capture=False, serial_port=None, pump_start_failure=False):
        with tempfile.TemporaryDirectory() as d, ExitStack() as stack:
            clock = FakeTime()
            capture = Capture(Path(d) / 'session.log', clock, guard_changes=guard_changes,
                              unstable=unstable, bad_ratio=bad_ratio, saturated=saturated)
            events = []
            class Master:
                def __init__(self, port):
                    self.port = port
                def start_pump(self, *, bpm):
                    events.append(('pump', bpm))
                    capture.active = True
                    if pump_start_failure:
                        raise RuntimeError('partial_pump_start_failed')
                def stop_pump(self):
                    if capture.active:
                        events.append(('retire',))
                        capture.active = False
                        capture.n += 4
                        capture.pending = True
                def pulses_sent(self):
                    return 99  # Deliberately NOT equal to measured macro count.
                def pump_error(self):
                    return RuntimeError('send failed') if pump_failure else None
            session = Mock(out_port=Mock())
            session.close.side_effect = lambda: events.append(('MIDIclose',))
            config = SimpleNamespace(out_dir=Path(d), serial_port=serial_port, midi_out_name='fake', midi_in_name='fake')
            stack.enter_context(patch('hitl.layered_cli.config_from_runner_args', return_value=config))
            stack.enter_context(patch('hitl.managed_capture.active_managed_capture', return_value=None if no_capture else capture))
            opener = stack.enter_context(patch('hitl.session.open_midi_session', return_value=session))
            stack.enter_context(patch('hitl.midi_clock.MidiClockMaster', Master))
            stack.enter_context(patch.object(cost.time, 'monotonic', clock.monotonic))
            stack.enter_context(patch.object(cost.time, 'sleep', clock.sleep))
            code = cost.run_ingress_cost(SimpleNamespace(legacy_args=list(options)))
            report = json.loads((Path(d) / 'ingress-cost.json').read_text())
            if opener.called:
                self.assertIsNone(opener.call_args.kwargs['serial_port'])
            self.assertFalse(capture.active)
            return code, report, capture, events

    def test_real_scenario_policy_two_clock_only_legs_and_stable_poll(self):
        code, report, capture, events = self.exercise(options=['--ingress-cost-mode', 'smoke'])
        self.assertEqual(code, 0, report['issues'])
        self.assertEqual([e for e in events if e[0] == 'pump'], [('pump', 120), ('pump', 300)])
        self.assertEqual(events[-1], ('MIDIclose',))
        self.assertTrue(capture.inflight)
        self.assertEqual(len(report['guards']), 6)
        self.assertGreaterEqual(len(report['observations']), 10)
        self.assertTrue(all(b - a >= .999 for a, b in zip(capture.cost_times, capture.cost_times[1:])))
        self.assertTrue(all(c.startswith(('!INGRESS_COST ', '!PIT_MOTION ')) for c in capture.commands))
        self.assertEqual(len(set(int(c.split()[1]) for c in capture.commands)), len(capture.commands))
        self.assertEqual(report['legs'][0]['count_sum_deltas'][0]['accepted'], 4)
        self.assertEqual(report['duration_s'], 2)
        self.assertIn('NOT RUN', report['other_routes']['1'])
        self.assertIn('self-accounting tail excluded/unmeasured', report['limitations']['publication'])

    def test_full_mode_is_five_seconds(self):
        code, report, _, _ = self.exercise()
        self.assertEqual(code, 0, report['issues'])
        self.assertEqual(report['duration_s'], 5)

    def test_guards_fail_closed_before_pump(self):
        for change in (dict(guardBits=14), dict(transport=1), dict(status=1), dict(era=0),
                       dict(configBits=0), dict(rawTicks=0)):
            code, report, _, events = self.exercise(guard_changes={1: change})
            self.assertEqual(code, 2, change)
            self.assertFalse(any(e[0] == 'pump' for e in events))
            self.assertTrue(report['issues'])

    def test_era_continuity_across_legs_and_post_guard(self):
        for index in (2, 3, 4, 6):
            code, report, _, events = self.exercise(guard_changes={index: dict(era=3)})
            self.assertEqual(code, 2)
            self.assertIn('changed_era', report['issues'][0])
            self.assertLessEqual(sum(e[0] == 'pump' for e in events), 2)

    def test_unstable_snapshot_budget_fails_before_pump(self):
        code, report, _, events = self.exercise(unstable=True)
        self.assertEqual(code, 2)
        self.assertIn('quiescent_snapshot_timeout', report['issues'][0])
        self.assertFalse(any(e[0] == 'pump' for e in events))

    def test_bad_quiescent_ratios_cannot_pass(self):
        code, report, _, _ = self.exercise(bad_ratio=True)
        self.assertEqual(code, 2)
        self.assertIn('ratios', report['issues'][0])

    def test_pump_failure_cleanup_before_ports_close(self):
        code, report, _, events = self.exercise(pump_failure=True)
        self.assertEqual(code, 2)
        self.assertIn('clock_pump_failed', report['issues'][0])
        self.assertLess(events.index(('retire',)), events.index(('MIDIclose',)))

    def test_partial_pump_start_failure_cleanup_before_close(self):
        code, report, _, events = self.exercise(pump_start_failure=True)
        self.assertEqual(code, 2)
        self.assertIn('partial_pump_start_failed', report['issues'][0])
        self.assertLess(events.index(('retire',)), events.index(('MIDIclose',)))

    def test_saturation_never_pumps(self):
        code, report, _, events = self.exercise(saturated=True)
        self.assertEqual(code, 2)
        self.assertIn('saturated', report['issues'][0])
        self.assertFalse(any(e[0] == 'pump' for e in events))

    def test_invalid_options_and_owner_write_failure_report(self):
        for options in (['--ingress-cost-settle-s', 'nan'], ['--ingress-cost-stable-budget-s', '16'],
                        ['--ingress-cost-max-run-s', '361'], ['--ingress-cost-mode', 'bad']):
            code, report, _, events = self.exercise(options=options)
            self.assertEqual(code, 2)
            self.assertFalse(events)
            self.assertTrue(report['issues'])
        for kw in (dict(no_capture=True), dict(serial_port='/dev/not-authorized')):
            code, report, _, events = self.exercise(**kw)
            self.assertEqual(code, 2)
            self.assertIn('managed_outer_serial_owner_required', report['issues'][0])
            self.assertFalse(events)


class RegistrationTests(unittest.TestCase):
    def test_strict_command_whitelist(self):
        from capture_session import is_allowed_hitl_command
        for cmd in ('!INGRESS_COST 0', '!INGRESS_COST 4294967295'):
            self.assertTrue(is_allowed_hitl_command(cmd))
        for cmd in ('!INGRESS_COST', '!INGRESS_COST -1', '!INGRESS_COST +1', '!INGRESS_COST 4294967296',
                    '!INGRESS_COST  1', '!INGRESS_COST 1 ', '!INGRESS_COST 1 2', '!INGRESS_COST 1\n', '!INGRESS_COST ١'):
            self.assertFalse(is_allowed_hitl_command(cmd), cmd)

    def test_temp_registration_safety_and_discovery(self):
        from hitl.registry import get_registry, resolve_scenarios
        from hitl.stimuli.registry import stimulus_for_scenario, list_stimuli, format_stimulus_list_lines
        spec = stimulus_for_scenario('ingress_cost')
        self.assertEqual(spec.stimulus_id, 'STIM-INGRESS-COST-TEMP-01')
        self.assertEqual(spec.kind, 'temporary')
        self.assertEqual(spec.base_bars, 0)
        self.assertFalse(spec.overdubs)
        self.assertIsNone(spec.authorization)
        self.assertNotIn(spec, list_stimuli())
        self.assertTrue(any(spec.stimulus_id in s for s in format_stimulus_list_lines(include_temporary=True)))
        self.assertFalse(get_registry()['ingress_cost'].owns_capture)
        self.assertEqual(resolve_scenarios(preset='ingress_cost', scenario_ids=None), ['ingress_cost'])
        p = argparse.ArgumentParser()
        cost.add_arguments(p)
        self.assertEqual(p.parse_args([]).ingress_cost_max_run_s, 60)

    def test_actual_master_clock_only_cleanup(self):
        import time
        from hitl.midi_clock import MidiClockMaster
        port = Mock()
        master = MidiClockMaster(port)
        try:
            master.start_pump(bpm=300)
            time.sleep(.04)
        finally:
            master.stop_pump()
        sent = [c.args[0].type for c in port.send.call_args_list]
        self.assertGreater(len(sent), 0)
        self.assertEqual(set(sent), {'clock'})
        count = len(sent)
        time.sleep(.02)
        self.assertEqual(port.send.call_count, count)
        self.assertIsNone(master.pump_error())


if __name__ == '__main__':
    unittest.main()
