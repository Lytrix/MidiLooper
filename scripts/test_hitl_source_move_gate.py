"""Source select / Move admission regressions; simulated I/O only."""
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parent))
import hitl.stimuli
from hitl.scenarios import note_edit_reselect_stacked as scenario
from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked
from test_hitl_reselect_validation import good_lines


class Collector:
    def __init__(self, rows=None): self.rows = list(rows or [])
    def snapshot(self): return list(self.rows)


class Clock:
    def __init__(self): self.now = 0.0
    def monotonic(self): return self.now
    def sleep(self, seconds): self.now += seconds


def move_rows(note_id=81, target=200):
    return ['[1.999] Received pitchbend: ch=14 value=-6077',
            f'[2.000] Note movement with overlap handling: from=392 to={target} difference=-192',
            f'[2.001] EditSessionAction: type=3 noteId={note_id} start={target} end={target+96} pitch=67']


class SourceMoveGateTests(unittest.TestCase):
    def test_verifier_accepts_exact_preserved_offset_move(self):
        rows = good_lines()
        rows[2] = '[1.010] Note movement with overlap handling: from=392 to=200 difference=-192'
        rows[3] = '[1.011] EditSessionAction: type=3 noteId=81 start=200 end=296 pitch=67'
        rows[5] = rows[5].replace('tick 192', 'tick 200')
        rows[7] = rows[7].replace('67,192,192', '67,200,200')
        rows[8] = rows[8].replace('tick 192', 'tick 200')
        rows[10] = rows[10].replace('64,192,192', '64,200,200')
        check = verify_note_edit_reselect_stacked(rows, SimpleNamespace(), source_tick=392,
                                                dest_tick=192, dest_note_tick=200)
        self.assertTrue(check['ok'], check['issues'])
        self.assertEqual(200, check['move_tick'])

    def test_wrong_offset_move_is_not_tolerated(self):
        rows = good_lines()
        rows[2] = '[1.010] Note movement with overlap handling: from=392 to=201 difference=-191'
        rows[3] = '[1.011] EditSessionAction: type=3 noteId=81 start=201 end=297 pitch=67'
        check = verify_note_edit_reselect_stacked(rows, SimpleNamespace(), source_tick=392,
                                                dest_tick=192, dest_note_tick=200)
        self.assertFalse(check['ok'])
        self.assertIn('selected_note_moved', check['issues'])

    def test_move_receipt_requires_expected_identity_and_geometry(self):
        for note_id, target in [(81,200), (82,200), (81,192)]:
            with self.subTest(note_id=note_id,target=target):
                clock=Clock(); collector=Collector(move_rows(note_id,target))
                with patch.object(scenario.time,'monotonic',clock.monotonic), patch.object(scenario.time,'sleep',clock.sleep):
                    result=scenario._wait_for_expected_move(collector,baseline=0,source_tick=392,
                            expected_tick=200,source_note_id=81,command_pitch=-6077,after_us=1000000,timeout_s=.1)
                self.assertEqual((81,200)==(note_id,target),result)

    def test_old_receipt_newly_delivered_cannot_prove_move(self):
        rows=[line.replace('[2.', '[0.') for line in move_rows()]
        clock=Clock()
        with patch.object(scenario.time,'monotonic',clock.monotonic), patch.object(scenario.time,'sleep',clock.sleep):
            self.assertFalse(scenario._wait_for_expected_move(Collector(rows),baseline=0,
                source_tick=392,expected_tick=200,source_note_id=81,command_pitch=-6077,after_us=1000000,timeout_s=.1))

    def test_missing_move_never_proceeds(self):
        clock=Clock()
        with patch.object(scenario.time,'monotonic',clock.monotonic), patch.object(scenario.time,'sleep',clock.sleep):
            self.assertFalse(scenario._wait_for_expected_move(Collector(['[2.000] No note selected for coarse editing']),
                baseline=0,source_tick=392,expected_tick=200,source_note_id=81,command_pitch=-6077,after_us=1000000,timeout_s=.1))

    def test_matching_move_without_current_coarse_input_refuses(self):
        clock=Clock()
        with patch.object(scenario.time,'monotonic',clock.monotonic), patch.object(scenario.time,'sleep',clock.sleep):
            self.assertFalse(scenario._wait_for_expected_move(Collector(move_rows()[1:]),baseline=0,
                source_tick=392,expected_tick=200,source_note_id=81,command_pitch=-6077,
                after_us=1000000,timeout_s=.1))

    def test_actual_send_hook_runs_after_wait_before_each_pitchbend(self):
        from hitl import legacy_edit_baseline as legacy
        events=[]
        class Port:
            def send(self,message): events.append('send')
        with patch.object(legacy,'_send_program_change'), \
             patch.object(legacy,'_wait_for_select_dependent_settle_clear',side_effect=lambda *a: events.append('wait')), \
             patch.object(legacy,'_coarse_settle_block_remain_ms',side_effect=[1,0]), \
             patch.object(legacy.time,'sleep'):
            legacy._send_coarse_pitchbend(Port(),-6077,serial_collector=Collector(),
                before_send=lambda: events.append('hook'))
        self.assertEqual(['wait','hook','send','hook','send'],events)

    def test_source_failure_prohibits_f2(self):
        layout=SimpleNamespace(loop_length=1536,step_to_tick={8:392,4:200})
        with patch('hitl.legacy_edit_baseline._fader1_select_fixture_note',return_value=False), \
             patch.object(scenario,'_select_step_until_apply',return_value=None), \
             patch('hitl.legacy_edit_baseline._fader2_move_to_sixteenth_step') as f2:
            self.assertFalse(scenario._select_source_and_move(object(),Collector(),layout=layout,
                source_step=8,dest_step=4,log_prefix='test'))
        f2.assert_not_called()

    def test_failed_stable_source_cannot_be_bypassed_by_weaker_fallback(self):
        layout=SimpleNamespace(loop_length=1536,step_to_tick={8:392,4:200})
        collector=Collector()
        def unconfirmed(*args,**kwargs):
            collector.rows.append('[1.001] NOTE_EDIT focus rebuild: noteId=81 presence=0 rowProjectsToStore=1')
            return False
        with patch('hitl.legacy_edit_baseline._fader1_select_fixture_note',side_effect=unconfirmed), \
             patch.object(scenario,'_select_step_until_apply',return_value=(392,2)), \
             patch('hitl.legacy_edit_baseline._fader2_move_to_sixteenth_step') as f2, \
             patch.object(scenario,'_wait_for_expected_move',return_value=True), \
             patch.object(scenario.time,'sleep'):
            self.assertFalse(scenario._select_source_and_move(object(),collector,layout=layout,
                source_step=8,dest_step=4,log_prefix='test'))
        f2.assert_not_called()

    def test_move_failure_prohibits_success_of_source_stage(self):
        layout=SimpleNamespace(loop_length=1536,step_to_tick={8:392,4:200})
        collector=Collector()
        def select(*args,**kwargs):
            collector.rows += ['[1.000] Select fader: selected note 2 at tick 392',
                               '[1.001] NOTE_EDIT focus rebuild: noteId=81 presence=0 rowProjectsToStore=1']
            return True
        with patch('hitl.legacy_edit_baseline._fader1_select_fixture_note',side_effect=select), \
             patch('hitl.legacy_edit_baseline._fader2_move_to_sixteenth_step',side_effect=lambda *a,**kw: kw['before_send']()), \
             patch.object(scenario,'_wait_for_expected_move',return_value=False) as wait, \
             patch.object(scenario.time,'sleep'):
            self.assertFalse(scenario._select_source_and_move(object(),collector,layout=layout,
                source_step=8,dest_step=4,log_prefix='test'))
        self.assertEqual(200,wait.call_args.kwargs['expected_tick'])
        self.assertEqual(81,wait.call_args.kwargs['source_note_id'])

    def test_move_delivered_before_actual_f2_send_cannot_qualify_command(self):
        layout=SimpleNamespace(loop_length=1536,step_to_tick={8:392,4:200})
        collector=Collector(); clock=Clock()
        def select(*args,**kwargs):
            collector.rows += ['[1.000] Select fader: selected note 2 at tick 392',
                               '[1.001] NOTE_EDIT focus rebuild: noteId=81 presence=0 rowProjectsToStore=1']
            return True
        def move(*args,**kwargs):
            collector.rows += move_rows()  # earlier motor-driven Move delivered during internal wait
            if kwargs.get('before_send'): kwargs['before_send']()
            collector.rows += ['[3.000] Received pitchbend: ch=14 value=-6077']
        with patch('hitl.legacy_edit_baseline._fader1_select_fixture_note',side_effect=select), \
             patch('hitl.legacy_edit_baseline._fader2_move_to_sixteenth_step',side_effect=move), \
             patch.object(scenario.time,'monotonic',clock.monotonic), \
             patch.object(scenario.time,'sleep',clock.sleep):
            self.assertFalse(scenario._select_source_and_move(object(),collector,layout=layout,
                source_step=8,dest_step=4,log_prefix='test'))

    def test_correct_source_and_move_producer_pass_stage(self):
        layout=SimpleNamespace(loop_length=1536,step_to_tick={8:392,4:200})
        collector=Collector()
        def select(*args,**kwargs):
            collector.rows += ['[1.000] Select fader: selected note 2 at tick 392',
                               '[1.001] NOTE_EDIT focus rebuild: noteId=81 presence=0 rowProjectsToStore=1']
            return True
        def move(*args,**kwargs):
            kwargs['before_send']()
            collector.rows += move_rows()
        with patch('hitl.legacy_edit_baseline._fader1_select_fixture_note',side_effect=select), \
             patch('hitl.legacy_edit_baseline._fader2_move_to_sixteenth_step',side_effect=move), \
             patch.object(scenario.time,'sleep'):
            self.assertTrue(scenario._select_source_and_move(object(),collector,layout=layout,
                source_step=8,dest_step=4,log_prefix='test'))


if __name__ == '__main__': unittest.main()
