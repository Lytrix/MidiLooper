"""Production-path regressions for run17's retained telemetry and host lifecycle."""
from __future__ import annotations

import json
import sys
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hitl.config import HitlConfig
from hitl import managed_capture as mc
from hitl.serial_timing import extract_phase_sessions
from hitl.transport_clock import ensure_transport_clock


class FreshStartTests(unittest.TestCase):
    def test_old_start_and_display_cannot_confirm_new_press(self):
        lines = ['[1.000] [INFO] Transport started', '#CAP,2000000,DFRAME,1']
        collector = SimpleNamespace(snapshot=lambda: lines)
        port = SimpleNamespace(poll=lambda: None, send=lambda _m: self.fail('must not toggle'))
        args = SimpleNamespace(serial_port='fake')
        self.assertEqual((False, False), ensure_transport_clock(
            port, port, collector, args, min_clocks=24, timeout_s=0,
            started_since_index=len(lines)))

    def test_fresh_display_without_start_is_not_an_acknowledgement(self):
        collector = SimpleNamespace(snapshot=lambda: ['#CAP,2000000,DFRAME,1'])
        port = SimpleNamespace(poll=lambda: None, send=lambda _m: self.fail('must not toggle'))
        self.assertEqual((False, False), ensure_transport_clock(
            port, port, collector, SimpleNamespace(serial_port='fake'),
            min_clocks=24, timeout_s=0, started_since_index=0))

    def test_fresh_start_is_accepted_without_toggle(self):
        collector = SimpleNamespace(snapshot=lambda: [
            '[1.000] [INFO] Transport started', '[2.000] [INFO] Transport stopped',
            '[3.000] [INFO] Transport started'])
        port = SimpleNamespace(poll=lambda: None, send=lambda _m: self.fail('must not toggle'))
        self.assertEqual((True, True), ensure_transport_clock(
            port, port, collector, SimpleNamespace(serial_port='fake'),
            min_clocks=24, timeout_s=0, started_since_index=2))


class MusicalTakeTests(unittest.TestCase):
    def _two_takes(self, first, second):
        return ['#CAP,100,ST,Track,ARMED,RECORDING', *first,
                '#CAP,200,ST,Track,RECORDING,STOPPED_RECORDING',
                '#CAP,300,ST,Track,ARMED,RECORDING', *second,
                '#CAP,400,ST,Track,RECORDING,STOPPED_RECORDING']

    def test_setup_controls_do_not_steal_musical_take(self):
        rows = self._two_takes(
            ['#CAP,120,MI,U,144,16,39,127', '#CAP,130,MI,U,144,16,38,127'],
            ['#CAP,320,MI,U,144,8,60,98', '#CAP,330,MI,U,144,8,64,98'])
        report = extract_phase_sessions(rows)
        self.assertEqual((300, 400), (report.record.start_ts, report.record.stop_ts))

    def test_greater_musical_count_wins_not_boolean_presence(self):
        rows = self._two_takes(['#CAP,120,MI,U,144,8,60,98'],
                              ['#CAP,320,MI,U,144,8,60,98', '#CAP,330,MI,U,144,8,64,98'])
        self.assertEqual(300, extract_phase_sessions(rows).record.start_ts)

    def test_zero_velocity_note_on_is_not_a_streamed_note(self):
        rows = self._two_takes(['#CAP,120,MI,U,144,8,60,0'], ['#CAP,320,MI,U,144,8,64,98'])
        self.assertEqual(300, extract_phase_sessions(rows).record.start_ts)

    def test_explicit_channel_including_16_is_respected(self):
        rows = self._two_takes(['#CAP,120,MI,U,144,8,60,98'], ['#CAP,320,MI,U,144,16,64,98'])
        self.assertEqual(300, extract_phase_sessions(rows, midi_channel_1based=16).record.start_ts)


class ManagedExitTests(unittest.TestCase):
    def _run(self, result=0, error=None, idle=True, watchdog=False):
        from hitl import device_shutdown as ds
        self.order = []
        self.shutdown = None
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            log = out / 'session.log'
            log.write_text('#CAP,1,SAVE,idle,0\n')
            config = HitlConfig(preset='test', scenario_ids=('test',), track_number=8,
                                loop_slot=4, midi_channel=8, record_bars=2, overdub_bars=0,
                                midi_out_name='Teensy', midi_in_name='Teensy', serial_port=None,
                                verify_serial_log=None, verify_only=False, out_dir=out,
                                boot_reset='never')
            @contextmanager
            def capture(_config):
                self.order.append('capture_open')
                try:
                    yield log
                finally:
                    self.order.append('capture_close')
            def stop_sender(_config, sent):
                def send():
                    if not sent.get('value'):
                        self.order.append('stop')
                        sent['value'] = True
                return send
            def scenario(_active):
                self.order.append('playing_leg')
                if error:
                    raise error
                return result
            with patch.object(mc, 'managed_capture_session', capture), \
                    patch.object(mc, 'active_managed_capture', return_value=None), \
                    patch.object(mc, 'establish_attach_readiness', return_value=(True, 'ready')), \
                    patch.object(mc, 'attach_stream_is_usable', return_value=(True, 'ready')), \
                    patch.object(mc, 'transport_stop_sender', stop_sender), \
                    patch.object(mc, 'write_metadata_best_effort'), \
                    patch.object(mc, '_run_with_watchdog', side_effect=lambda fn, _timeout: 3 if watchdog else fn()), \
                    patch.object(ds, '_wait_for_verified_idle', return_value=idle), \
                    patch.object(ds, 'mark_incomplete') as latch:
                try:
                    code = mc.run_with_managed_capture(config, scenario, timeout_s=0)
                finally:
                    path = out / 'shutdown-result.json'
                    if path.exists():
                        self.shutdown = json.loads(path.read_text())
                    self.latched = latch.called
            return code

    def test_success_stops_twice_before_capture_release(self):
        self.assertEqual(0, self._run())
        self.assertEqual(['capture_open', 'playing_leg', 'stop', 'stop', 'capture_close'], self.order)
        self.assertTrue(self.shutdown['ok'])

    def test_failed_leg_still_stops_twice(self):
        self.assertEqual(1, self._run(result=1))
        self.assertEqual(2, self.order.count('stop'))
        self.assertEqual('capture_close', self.order[-1])

    def test_exception_still_drains_before_reraise(self):
        with self.assertRaisesRegex(RuntimeError, 'leg failed'):
            self._run(error=RuntimeError('leg failed'))
        self.assertEqual(['stop', 'stop', 'capture_close'], self.order[-3:])

    def test_incomplete_shutdown_is_not_success_and_latches(self):
        self.assertNotEqual(0, self._run(idle=False))
        self.assertTrue(self.latched)
        self.assertFalse(self.shutdown['ok'])


    def test_watchdog_exit_still_drains_before_capture_release(self):
        self.assertEqual(3, self._run(watchdog=True))
        self.assertEqual(['capture_open', 'stop', 'stop', 'capture_close'], self.order)
        self.assertTrue(self.shutdown['ok'])

    def test_failed_boot_stop_is_not_cached_and_failure_propagates(self):
        from unittest.mock import MagicMock
        sent = {'value': False}
        port = MagicMock()
        opened = MagicMock()
        opened.__enter__.return_value = port
        sender = mc.transport_stop_sender(SimpleNamespace(midi_out_name='Teensy'), sent)
        with patch('hitl.midi_io._find_midi_port', return_value='Teensy MIDI'), \
                patch('mido.open_output', side_effect=[RuntimeError('disconnected'), opened]) as open_port:
            with self.assertRaisesRegex(RuntimeError, 'disconnected'):
                sender()
            self.assertFalse(sent['value'])
            sender()
            sender()
        self.assertTrue(sent['value'])
        self.assertEqual(2, open_port.call_count)
        self.assertEqual(1, port.send.call_count)

class TrackStatusTests(unittest.TestCase):
    def test_protocol_validates_exact_status(self):
        from hitl.serial.protocol import selected_track_status
        row = '#TRACK_STATUS,us=100,track=7,slot=3,state=PLAYING,transport=1'
        self.assertEqual({'us': 100, 'track': 7, 'slot': 3, 'state': 'PLAYING', 'transport': 1},
                         selected_track_status([row]))
        for bad in (row.replace('track=7', 'track=8'), row.replace('transport=1', 'transport=2'),
                    row.replace('state=PLAYING', 'state=garbage'), row + ',track=0',
                    row.replace(',slot=3', ''), row.replace('us=100', 'us=-1')):
            self.assertIsNone(selected_track_status([bad]), bad)

    def test_broker_accepts_only_argument_free_track_status(self):
        from capture_session import is_allowed_hitl_command
        self.assertTrue(is_allowed_hitl_command('!TRACK_STATUS'))
        self.assertFalse(is_allowed_hitl_command('!TRACK_STATUS 7'))

    def _guard(self, statuses):
        from hitl.scenarios.note_edit_reselect_stacked import _ensure_target_playing
        capture = SimpleNamespace(selected_track_status=lambda **_kw: statuses.pop(0) if statuses else None)
        collector = SimpleNamespace(snapshot=lambda: ['#CAP,1,ST,Track,PLAYING,STOPPED'])
        with patch.object(mc, 'active_managed_capture', return_value=capture), \
                patch('hitl.midi_io._send_short_press') as press:
            ready = _ensure_target_playing(None, collector, press_ms=120, phase_wait_ms=0,
                                           log_prefix='test', track_number=8, loop_slot=4)
        return ready, press.call_count

    def test_live_status_works_when_playing_rows_stay_in_ring(self):
        stopped = {'track': 7, 'slot': 3, 'state': 'STOPPED', 'transport': 0}
        playing = dict(stopped, state='PLAYING', transport=1)
        self.assertEqual((True, 1), self._guard([stopped, playing]))
        self.assertEqual((True, 0), self._guard([playing]))

    def test_wrong_target_or_missing_query_never_toggles(self):
        for status in (None, {'track': 6, 'slot': 3, 'state': 'PLAYING', 'transport': 1},
                       {'track': 7, 'slot': 2, 'state': 'PLAYING', 'transport': 1},
                       {'track': 7, 'slot': 3, 'state': 'PLAYING', 'transport': 0}):
            self.assertEqual((False, 0), self._guard([status]))

    def test_scenario_parser_keeps_the_explicit_slot(self):
        from hitl.scenarios.note_edit_reselect_stacked import _parse_scenario_args
        args = SimpleNamespace(legacy_args=['--track-number', '8', '--loop-slot', '4'],
                               out_dir=Path('captures'))
        parsed = _parse_scenario_args(args)
        self.assertEqual((8, 4), (parsed.track_number, parsed.loop_slot))

    def test_live_broker_framing_is_wired_to_the_production_dispatch(self):
        import io
        import capture_session as broker
        source = (Path(__file__).resolve().parent.parent / 'src/StorageManager/StorageManagerHitlSerial.cpp').read_text()
        self.assertIn('if (processLiveObservationCommand(sHitlSerialLineBuffer, printSelectedTrackStatusForHitl))', source)
        sink = SimpleNamespace(is_open=True, writes=[])
        sink.write = sink.writes.append
        sink.flush = lambda: None
        start, end = 'a' * 32, 'b' * 32
        stream = io.StringIO(f'{start} {end} !TRACK_STATUS\n')
        with patch.object(broker.sys, 'stdin', stream), \
                patch.object(broker.select, 'select', return_value=([stream], [], [])):
            broker._service_command_requests(sink, enabled=True)
        self.assertEqual([f'!TRACE_FENCE {start}\n'.encode(), b'!TRACK_STATUS\n',
                          f'!TRACE_FENCE {end}\n'.encode()], sink.writes)


if __name__ == '__main__':
    unittest.main()
