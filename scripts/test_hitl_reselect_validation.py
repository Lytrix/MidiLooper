"""Adversarial qualification regressions; no device access."""
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parent))
import hitl.stimuli  # Public CLI import order avoids the legacy stimuli import cycle.
from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked, applied_selects


def good_lines():
    return [
        'Edit session: NOTE_EDIT',
        '[1.000] NOTE_EDIT focus rebuild: noteId=81 presence=0 rowProjectsToStore=1',
        '[1.010] Note movement with overlap handling: from=384 to=192 difference=-192',
        '[1.011] EditSessionAction: type=3 noteId=81 start=192 end=288 pitch=67',
        '[2.000] Select fader: selected empty step at tick 48 (no note)',
        '[3.000] Select fader: selected note 3 at tick 192',
        '[3.001] NOTE_EDIT focus rebuild: noteId=81 presence=0 rowProjectsToStore=1',
        '#CAP,3100000,DNTE,67,192,192,96,3',
        '[4.000] Select fader: selected note 5 at tick 192',
        '[4.001] NOTE_EDIT focus rebuild: noteId=99 presence=0 rowProjectsToStore=1',
        '#CAP,4100000,DNTE,64,192,192,96,5',
    ]


def check(lines):
    return verify_note_edit_reselect_stacked(lines, SimpleNamespace())


class ReselectQualificationTests(unittest.TestCase):
    def test_complete_core_proof_passes(self):
        self.assertTrue(check(good_lines())['ok'])

    def test_nonempty_select_is_not_deselect(self):
        rows=good_lines(); rows[4]='[2.000] Select fader: selected note 7 at tick 48'
        result=check(rows)
        self.assertFalse(result['ok']); self.assertIn('deselect_empty_step', result['issues'])

    def test_move_to_wrong_destination_rejected(self):
        rows=good_lines(); rows[3]=rows[3].replace('start=192', 'start=960')
        self.assertFalse(check(rows)['ok'])

    def test_move_from_wrong_source_rejected(self):
        rows=good_lines(); rows[2]=rows[2].replace('from=384', 'from=960')
        self.assertFalse(check(rows)['ok'])

    def test_move_wrong_note_id_rejected(self):
        rows=good_lines(); rows[3]=rows[3].replace('noteId=81', 'noteId=55')
        self.assertFalse(check(rows)['ok'])

    def test_move_wrong_pitch_rejected(self):
        rows=good_lines(); rows[3]=rows[3].replace('pitch=67', 'pitch=20')
        self.assertFalse(check(rows)['ok'])

    def test_two_indexes_for_same_note_do_not_prove_stack(self):
        rows=good_lines(); rows[9]=rows[9].replace('noteId=99', 'noteId=81')
        self.assertFalse(check(rows)['ok'])

    def test_wrong_destination_note_pitch_rejected(self):
        rows=good_lines(); rows[10]=rows[10].replace('DNTE,64,', 'DNTE,20,')
        self.assertFalse(check(rows)['ok'])

    def test_absent_focus_identity_is_unqualified(self):
        self.assertFalse(check([x for x in good_lines() if 'focus rebuild' not in x])['ok'])

    def test_absent_note_geometry_is_unqualified(self):
        self.assertFalse(check([x for x in good_lines() if ',DNTE,' not in x])['ok'])

    def test_delayed_twins_do_not_change_result_or_counts(self):
        rows=good_lines(); twins=[
            '#DBG select_apply selected_tick=48 note_idx=-1 slot=1 prior_slot=0 apply=1 reason=empty',
            '#DBG select_apply selected_tick=192 note_idx=3 slot=2 prior_slot=1 apply=1 reason=nav',
            '#DBG select_apply selected_tick=192 note_idx=5 slot=3 prior_slot=2 apply=1 reason=nav',
        ]
        result=check(rows+twins)
        self.assertTrue(result['ok'], result['issues'])
        self.assertEqual(result['dest_note_idxs'], check(rows)['dest_note_idxs'])
        self.assertEqual(len(applied_selects(rows)), len(applied_selects(rows+twins)))

    def test_unstamped_deferred_rows_cannot_invent_chronology(self):
        rows=[x for x in good_lines() if 'Select fader' not in x]
        rows += [
            '#DBG select_apply selected_tick=48 note_idx=-1 slot=1 prior_slot=0 apply=1 reason=empty',
            '#DBG select_apply selected_tick=192 note_idx=3 slot=2 prior_slot=1 apply=1 reason=nav',
            '#DBG select_apply selected_tick=192 note_idx=5 slot=3 prior_slot=2 apply=1 reason=nav',
        ]
        self.assertFalse(check(rows)['ok'])

    def test_missing_optional_evidence_is_unmeasured(self):
        result=check(good_lines())
        for name in ['playing_motor_value_only', 'value_only_notegate_held',
                     'select_apply_highlight_follows', 'moved_note_kept_after_deselect']:
            self.assertEqual('UNMEASURED', result[name]['status'])
            self.assertIsNone(result[name]['ok'])

    def test_historical_deselect_cannot_satisfy_current_move(self):
        rows=good_lines(); rows[4]='[0.100] Select fader: selected empty step at tick 48'
        self.assertFalse(check(rows)['ok'])

    def test_prior_success_cannot_qualify_new_failed_session(self):
        rows = good_lines() + [
            '[10.000] Edit session: NOTE_EDIT',
            '[10.010] Note movement with overlap handling: from=384 to=960 difference=576',
            '[10.011] EditSessionAction: type=3 noteId=99 start=960 end=1056 pitch=67',
        ]
        self.assertFalse(check(rows)['ok'])

    def test_replay_without_observed_seed_refuses(self):
        from hitl.scenarios.note_edit_reselect_stacked import verify_note_edit_reselect_stacked as replay
        result = replay(good_lines(), SimpleNamespace(midi_channel=8, legacy_args=[]))
        self.assertFalse(result['ok'])
        self.assertEqual('INCONCLUSIVE_CAPTURE', result['verdict'])

    def test_replay_requires_both_observed_fixture_pitches(self):
        from hitl.scenarios.note_edit_reselect_stacked import verify_note_edit_reselect_stacked as replay
        from host_midi_automation_edit_baseline import EDIT_RECORD_FIXTURE
        prefix = ['#CAP,100,RECA,0,0', '#CAP,200,RECS,stop,3,0,0,1536,1536,0']
        for missing in (64, 67):
            with self.subTest(missing=missing):
                seed = prefix + [f'#CAP,{201+i},REVT,{n.step*48},8,{n.pitch}'
                                 for i,n in enumerate(EDIT_RECORD_FIXTURE) if n.pitch != missing]
                result = replay(seed + good_lines(), SimpleNamespace(midi_channel=8, legacy_args=[]))
                self.assertFalse(result['ok'])
                self.assertEqual('INCONCLUSIVE_CAPTURE', result['verdict'])


    def test_observed_seed_and_current_identity_proof_pass_replay(self):
        from hitl.scenarios.note_edit_reselect_stacked import verify_note_edit_reselect_stacked as replay
        from host_midi_automation_edit_baseline import EDIT_RECORD_FIXTURE
        seed = ['#CAP,100,RECA,0,0', '#CAP,200,RECS,stop,3,0,0,1536,1536,0']
        seed += [f'#CAP,{201+i},REVT,{n.step*48},8,{n.pitch}' for i,n in enumerate(EDIT_RECORD_FIXTURE)]
        result = replay(seed + good_lines(), SimpleNamespace(midi_channel=8, legacy_args=[]))
        self.assertTrue(result['ok'], result['issues'])
        self.assertEqual('PASS_CORE', result['verdict'])

    def test_live_boundary_excludes_prior_success_without_a_new_entry(self):
        prior = good_lines()
        rows = prior + ['[10.010] Note movement with overlap handling: from=384 to=960 difference=576',
                        '[10.011] EditSessionAction: type=3 noteId=99 start=960 end=1056 pitch=67']
        result = verify_note_edit_reselect_stacked(rows, SimpleNamespace(), after_index=len(prior))
        self.assertFalse(result['ok'])

    def test_ambiguous_multiple_move_replay_refuses(self):
        rows = good_lines() + ['[10.010] Note movement with overlap handling: from=384 to=960 difference=576']
        result = check(rows)
        self.assertFalse(result['ok'])
        self.assertIn('ambiguous_edit_replay_window', result['issues'])


class FocusGeometryMarkerTests(unittest.TestCase):
    def rows(self):
        rows = good_lines()
        rows[6] += ' pitch=67 start=192 end=288'
        rows[9] += ' pitch=64 start=192 end=288'
        return [row for row in rows if ',DNTE,' not in row]

    def test_current_focus_geometry_without_dnte_qualifies_core(self):
        result = check(self.rows())
        self.assertTrue(result['ok'], result['issues'])
        self.assertEqual('PASS_CORE', result['verdict'])
        self.assertEqual([81, 99], result['dest_note_ids'])
        self.assertFalse(result['qualification_complete'])

    def test_invalid_current_focus_geometry_refuses(self):
        for old, new in [('pitch=67', 'pitch=20'), ('start=192', 'start=193'),
                         ('end=288', 'end=192'), ('presence=0', 'presence=1'),
                         ('rowProjectsToStore=1', 'rowProjectsToStore=0'),
                         ('noteId=81', 'noteId=99')]:
            with self.subTest(change=new):
                rows = self.rows()
                rows[6] = rows[6].replace(old, new)
                self.assertFalse(check(rows)['ok'])

    def test_geometry_timestamp_outside_select_interval_refuses(self):
        for replacement in ('[0.001]', '[4.100]'):
            with self.subTest(stamp=replacement):
                rows = self.rows()
                rows[6] = rows[6].replace('[3.001]', replacement)
                self.assertFalse(check(rows)['ok'])

    def test_wrong_final_geometry_cannot_be_hidden_by_correct_dnte(self):
        rows = good_lines()
        rows[6] += ' pitch=20 start=192 end=288'
        self.assertFalse(check(rows)['ok'])

    def test_two_focus_geometries_for_one_selection_refuse(self):
        rows = self.rows()
        rows.insert(7, '[3.002] NOTE_EDIT focus rebuild: noteId=81 presence=0 '
                       'rowProjectsToStore=1 pitch=67 start=193 end=289')
        self.assertFalse(check(rows)['ok'])

    def test_direct_geometry_cannot_qualify_prior_or_other_session(self):
        rows = self.rows() + ['[10.000] Edit session: NOTE_EDIT',
                              '[10.010] Select fader: selected note 3 at tick 192']
        self.assertFalse(check(rows)['ok'])

    def test_delayed_display_index_reuse_cannot_override_current_focus_geometry(self):
        rows = self.rows()
        rows += ['[4.500] Edit session: LOOP_EDIT',
                 '#CAP,5100000,DNTE,60,584,584,96,5']
        result = check(rows)
        self.assertTrue(result['ok'], result['issues'])


if __name__ == '__main__':
    unittest.main()
