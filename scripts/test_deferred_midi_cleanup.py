"""Managed shutdown must precede potentially blocking native MIDI destruction."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from contextlib import contextmanager
from unittest.mock import patch

from hitl import managed_capture as managed
from hitl.config import HitlConfig
from hitl.session import HitlSession


class DeferredMidiCleanupTests(unittest.TestCase):
    def run_fixture(self, out, close, *, scenario_error=False, idle=True):
        events = []
        log = out / 'serial.log'
        log.write_text('PERS,idle\n')
        capture = SimpleNamespace(_deferred_midi_sessions=[], _process=None, session_log=log)
        capture.defer_midi_close = lambda session: capture._deferred_midi_sessions.append(session)
        capture.send_command = lambda *_a, **_k: (events.append('heap') or True, '')
        capture.fenced_response = lambda *_a, **_k: ['#HEAP_STATS,fixture']
        capture.complete_command = lambda *_a: None

        @contextmanager
        def capture_boundary(_config):
            try:
                yield log
            finally:
                managed.stop_transport_and_observe_heap(capture)
                events.append('capture-stop')

        def drain(*_args, **_kwargs):
            events.append('drain')
            return idle

        def scenario(_config):
            session = HitlSession(out_port=SimpleNamespace(close=lambda: close(events),
                send=lambda message: events.append(message.type)), in_port=None)
            capture.defer_midi_close(session)
            events.append('scenario')
            if scenario_error:
                raise ValueError('original-scenario-failure')
            return 2  # An oracle failure must not be upgraded by successful cleanup.

        config = HitlConfig(preset=None, scenario_ids=(), track_number=8, loop_slot=4,
                            midi_channel=8, serial_port=None, out_dir=out, record_bars=0,
                            overdub_bars=0, midi_out_name='Teensy', midi_in_name='Teensy',
                            verify_serial_log=None, verify_only=False, boot_reset='never')
        with patch.object(managed, 'managed_capture_session', capture_boundary), \
             patch.object(managed, 'active_managed_capture', return_value=capture), \
             patch.object(managed, 'establish_attach_readiness', return_value=(True, 'fixture')), \
             patch.object(managed, 'attach_stream_is_usable', return_value=(True, 'fixture')), \
             patch.object(managed, 'write_metadata_best_effort'), \
             patch('mido.open_output', side_effect=AssertionError('teardown must reuse retained output')), \
             patch('hitl.device_shutdown._wait_for_verified_idle', side_effect=drain), \
             patch('hitl.device_shutdown.mark_incomplete'):
            try:
                code = managed.run_with_managed_capture(config, scenario, timeout_s=2)
            except BaseException:
                self.events = events
                raise
        return code, events

    def test_failure_verdict_preserved_and_close_follows_two_drains_and_capture_stop(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            code, events = self.run_fixture(out, lambda events: events.append('close'))
            self.assertEqual(code, 2)
            self.assertEqual(events, ['scenario', 'stop', 'drain', 'stop', 'drain', 'heap', 'stop', 'heap', 'capture-stop', 'close'])
            self.assertTrue(json.loads((out / 'shutdown-result.json').read_text())['ok'])
            self.assertTrue(json.loads((out / 'midi-cleanup-result.json').read_text())['ok'])

    def test_scenario_exception_still_drains_then_closes(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, 'original-scenario-failure'):
                self.run_fixture(Path(tmp), lambda events: events.append('close'), scenario_error=True)
            self.assertEqual(self.events[1:], ['stop', 'drain', 'stop', 'drain', 'heap', 'stop', 'heap', 'capture-stop', 'close'])

    def test_close_exception_is_durable_failure_after_safety(self):
        def fail(events):
            events.append('close')
            raise OSError('backend-close')
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            with self.assertRaisesRegex(OSError, 'backend-close'):
                self.run_fixture(out, fail)
            result = json.loads((out / 'midi-cleanup-result.json').read_text())
            self.assertFalse(result['ok'])
            self.assertEqual(result['status'], 'failed')
            self.assertEqual(result['stage'], 'out_port')
            self.assertTrue(json.loads((out / 'shutdown-result.json').read_text())['ok'])

    def test_incomplete_shutdown_never_arms_fatal_close(self):
        with tempfile.TemporaryDirectory() as tmp, patch('faulthandler.dump_traceback_later') as watchdog:
            with self.assertRaisesRegex(RuntimeError, 'requires_verified_shutdown'):
                self.run_fixture(Path(tmp), lambda _events: self.fail('unsafe close'), idle=False)
            watchdog.assert_not_called()

    def test_registration_is_identity_idempotent(self):
        with tempfile.TemporaryDirectory() as tmp:
            capture = managed.ManagedCaptureSession(out_dir=Path(tmp))
            session = HitlSession(out_port=None, in_port=None)
            capture.defer_midi_close(session)
            capture.defer_midi_close(session)
            self.assertEqual(capture._deferred_midi_sessions, [session])

    def test_live_capture_child_never_arms_fatal_close(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            capture = SimpleNamespace(_deferred_midi_sessions=[object()],
                                      _process=SimpleNamespace(poll=lambda: None))
            @contextmanager
            def boundary(_config):
                yield out / 'serial.log'
                (out / 'shutdown-result.json').write_text(json.dumps(dict(ok=True, drains=2)))
                capture._process = None  # stop lost its handle, but child did not exit.
            with patch.object(managed, 'managed_capture_session', boundary), \
                 patch.object(managed, 'active_managed_capture', return_value=capture), \
                 patch.object(managed, '_close_deferred_midi') as close:
                with self.assertRaisesRegex(RuntimeError, 'requires_verified_shutdown'):
                    with managed._capture_with_deferred_midi_cleanup(SimpleNamespace(out_dir=out)):
                        pass
                close.assert_not_called()

    def test_blocking_close_exits_child_nonzero_after_real_shutdown_owner(self):
        with tempfile.TemporaryDirectory() as tmp:
            env = dict(os.environ, PYTHONPATH=str(Path(__file__).resolve().parent))
            script = '''
import sys, ctypes
from pathlib import Path
from unittest.mock import patch
from test_deferred_midi_cleanup import DeferredMidiCleanupTests
from hitl import managed_capture as managed
out = Path(sys.argv[1])
real_close = managed._close_deferred_midi
def bounded(sessions, out_dir):
    return real_close(sessions, out_dir, timeout_s=.3)
def stuck(events):
    (out / 'order.json').write_text(__import__('json').dumps(events + ['close']))
    ctypes.PyDLL(None).sleep(30)  # Native call deliberately retains the GIL.
with patch.object(managed, '_close_deferred_midi', bounded):
    DeferredMidiCleanupTests().run_fixture(out, stuck)
'''
            start = time.monotonic()
            result = subprocess.run([sys.executable, '-c', script, tmp], env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=6)
            self.assertNotEqual(result.returncode, 0)
            self.assertLess(time.monotonic() - start, 6)
            out = Path(tmp)
            self.assertEqual(json.loads((out / 'order.json').read_text()),
                             ['scenario', 'stop', 'drain', 'stop', 'drain', 'heap', 'stop', 'heap', 'capture-stop', 'close'])
            self.assertTrue(json.loads((out / 'shutdown-result.json').read_text())['ok'])
            cleanup = json.loads((out / 'midi-cleanup-result.json').read_text())
            self.assertFalse(cleanup['ok'])
            self.assertEqual(cleanup['status'], 'closing')
            self.assertEqual(cleanup['stage'], 'out_port')
            self.assertIn('stuck', (out / 'midi-close-stacks.log').read_text())


if __name__ == '__main__':
    unittest.main()
