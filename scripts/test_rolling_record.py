import unittest

from capture_session import is_allowed_hitl_command
from hitl.verify.rolling_record import FIELDS, parse_snapshot, qualify, validate_sequence, wire_inventory, content_inventory, wire_schedule, stop_handoff, root_return_completed
from hitl.stimuli.registry import STIM_ROLLING_RECORD_TEMP_01, stimulus_for_scenario


def sample(**changes):
    s = {name: 0 for name in FIELDS}
    s.update(id=7, valid=1, track=7, slot=3, state=3, tick=3136, epoch=4, phase=3,
             frameOrigin=3072, framePeriod=2304, boundRole=1, boundStorage=9,
             currentRole=1, currentStorage=9, published=1, complete=1, sourceOrigin=2,
             sourceGeneration=6, captureToken=20, liveToken=20, start=0, length=768,
             period=2304, rows=2, cursor=0, ledger=0, firstTick=192, firstNoteId=1,
             lastTick=288, lastNoteId=1, rawCount=6, rawMax=1824, recording=1)
    s.update(frameEpoch=4, requestEpoch=4)
    s.update(changes)
    return s


def row(s):
    return '#CAP,12345,DIAG,rolling_record,' + ','.join(str(s[name]) for name in FIELDS)


class RollingRecordTests(unittest.TestCase):
    def test_schema_and_source(self):
        self.assertEqual(len(FIELDS), 43)
        parsed = parse_snapshot([row(sample())], 7)
        self.assertEqual(qualify(parsed, slot=4)['captureToken'], 20)
        self.assertEqual(qualify(sample(sourceGeneration=0), slot=4)['sourceGeneration'], 0)
        with self.assertRaises(RuntimeError):
            qualify(sample(frameEpoch=3), slot=4)

    def test_absent_duplicate_and_request_mismatch(self):
        for rows, request in [([], 7), ([row(sample())] * 2, 7), ([row(sample())], 8)]:
            with self.subTest(rows=rows, request=request), self.assertRaises(RuntimeError):
                parse_snapshot(rows, request)

    def test_unsigned_boolean_and_signed_ranges(self):
        for key, value in [('tick', -1), ('epoch', 1 << 32), ('currentStorage', 1 << 64),
                           ('published', 2), ('frameOrigin', 1 << 31), ('readyRole', 3),
                           ('readyFlags', 16), ('buildFlags', 16), ('workerFlags', 4)]:
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                parse_snapshot([row(sample(**{key: value}))], 7)
        self.assertEqual(parse_snapshot([row(sample(frameOrigin=-32))], 7)['frameOrigin'], -32)

    def test_wrong_target_partial_stale_binding_and_origin(self):
        for changes in [dict(track=6), dict(slot=0), dict(valid=0), dict(recording=0),
                        dict(phase=0), dict(currentStorage=10), dict(currentRole=2),
                        dict(published=0), dict(complete=0), dict(sourceOrigin=0),
                        dict(start=768), dict(length=1536), dict(period=0), dict(cursor=3)]:
            with self.subTest(changes=changes), self.assertRaises(RuntimeError):
                qualify(sample(**changes), slot=4)

    def test_empty_interval_is_valid(self):
        self.assertTrue(qualify(sample(tick=3904, start=768, rows=0,
                                      firstNoteId=0, lastNoteId=0), slot=4))

    def test_sequence_rejects_reused_requests_era_rebase_and_operation(self):
        a = sample()
        validate_sequence([a, sample(id=8, tick=3144)])
        for b in [sample(id=7, tick=3144), sample(id=8, tick=3100),
                  sample(id=8, epoch=5), sample(id=8, sourceGeneration=7)]:
            with self.subTest(b=b), self.assertRaises(RuntimeError):
                validate_sequence([a, b])

    def test_wire_exact_inventory_and_zero_velocity_normalization(self):
        rows = [dict(host_mono=2, type='note_on', channel=7, note=60, velocity=98),
                dict(host_mono=3, type='note_on', channel=7, note=60, velocity=0)]
        self.assertTrue(wire_inventory(rows, start=1, end=4, channel=7, pitches=[60])['ok'])
        for invalid in [[], rows[:1], rows + rows[:1],
                        list(reversed(rows)), [dict(r, note=61) for r in rows]]:
            with self.subTest(invalid=invalid), self.assertRaises(RuntimeError):
                wire_inventory(invalid, start=1, end=4, channel=7, pitches=[60])
        # Input monitor notes outside the input-free replay window are no proof.
        with self.assertRaises(RuntimeError):
            wire_inventory(rows, start=4, end=5, channel=7, pitches=[60])

    def test_content_requires_exact_effective_rows_and_paired_identity(self):
        rows = ['CAPTURE_CONTENT,begin,track=8,slot=3,length=768']
        for kind in ['record', 'effective']:
            rows += [f'CAPTURE_CONTENT,event,{kind},0,192,144,8,60,98,1',
                     f'CAPTURE_CONTENT,event,{kind},0,288,128,8,60,0,1']
        rows += ['CAPTURE_CONTENT,end']
        self.assertEqual(len(content_inventory(rows, pitches=[60], channel=8)['record']), 2)
        for invalid in [rows[:-1], rows + [rows[-1]],
                        [r.replace('288,128,8,60,0,1', '288,128,8,60,0,2') for r in rows],
                        [r for r in rows if ',effective,' not in r]]:
            with self.subTest(invalid=invalid), self.assertRaises(RuntimeError):
                content_inventory(invalid, pitches=[60], channel=8)

    def test_scoped_temporary_registration(self):
        spec = stimulus_for_scenario('rolling_record')
        self.assertIs(spec, STIM_ROLLING_RECORD_TEMP_01)
        self.assertEqual(spec.kind, 'temporary')
        self.assertTrue(spec.authorization.allows(track=8, slots=(4, 5)))
        self.assertFalse(spec.authorization.allows(track=7, slots=(4,)))
        self.assertFalse(spec.authorization.allows(track=8, slots=(1,)))

    def test_managed_command_grammar(self):
        for good in ['!ROLLING_RECORD_STATUS 0', '!ROLLING_RECORD_STATUS 4294967295']:
            self.assertTrue(is_allowed_hitl_command(good))
        for bad in ['!ROLLING_RECORD_STATUS', '!ROLLING_RECORD_STATUS -1',
                    '!ROLLING_RECORD_STATUS +1', '!ROLLING_RECORD_STATUS 4294967296',
                    '!ROLLING_RECORD_STATUS 1 ', '!ROLLING_RECORD_STATUS 1 2',
                    '!ROLLING_RECORD_STATUS 1\n!REBOOT']:
            self.assertFalse(is_allowed_hitl_command(bad))

    def test_phase_placement_rejects_displaced_output(self):
        notes = [dict(host_mono=2, note=60, velocity=98), dict(host_mono=3, note=60, velocity=0)]
        accepted = [dict(tick=192, type=144, note=60, velocity=98, noteId=1),
                    dict(tick=288, type=128, note=60, velocity=0, noteId=1)]
        samples = [sample(tick=3200, host_before=1, host_after=1.1),
                   sample(tick=3456, host_before=3.9, host_after=4)]
        self.assertTrue(wire_schedule(notes, accepted, samples, sample())['ok'])
        with self.assertRaises(RuntimeError):
            wire_schedule(notes, accepted, [dict(s, tick=s['tick'] + 768) for s in samples], sample())

    def test_stop_matching_preserves_the_same_physical_reader(self):
        """A non-truncating Stop keeps the same view; the frame may be cleared or still bound.

        Measured on hardware: the positional frame is cleared (`boundRole`/`boundStorage`/`framePeriod`) or
        left bound as a serving frame, while the serving view's `(currentRole, currentStorage)` is unchanged.
        Comparing those fields for equality — the pre-DEC-137 contract — could therefore never hold.

        DEC-141 replaced the two terms that were also unsatisfiable, both by measurement: the clock `epoch`
        does not change across a Record stop (`4 -> 4` and `8 -> 8` on two takes), and the served token is
        the pre-Stop *serving* receipt, not the pre-Stop *live* one (which advances with accepted input —
        measured serving token 7 while live had reached 9). The Stop-processed fact is the take leaving
        RECORDING while the job takes ownership.
        """
        before = sample(rawMax=288)
        stop = dict(raw_length=2304, final_length=2304)
        # The era is deliberately left equal to the pre-Stop sample: that is what the device does.
        after = sample(recording=0, stopJob=1, epoch=4, frameEpoch=4, frameOrigin=0, framePeriod=0,
                       boundRole=255, boundStorage=0)
        self.assertEqual(stop_handoff(before, [after], stop, slot=4),
                         'matching_physical_reader_preserved')
        self.assertEqual(stop_handoff(before, [after], dict(raw_length=2000, final_length=2304), slot=4),
                         'matching_physical_reader_preserved')
        for label, changes, term in [
                ('reader replaced', dict(currentRole=2, currentStorage=10),
                 'stop_matching_reader_not_preserved'),
                ('token is not the take receipt', dict(captureToken=21),
                 'stop_frozen_geometry_or_token_mismatch'),
                ('geometry is not the frozen take', dict(period=1536),
                 'stop_frozen_geometry_or_token_mismatch'),
                ('not published', dict(published=0), 'stop_frozen_geometry_or_token_mismatch'),
                ('not the Capture origin', dict(sourceOrigin=0), 'stop_frozen_geometry_or_token_mismatch'),
                ('cleared frame still names a storage', dict(boundStorage=9),
                 'stop_matching_frame_inconsistent'),
                ('stop job already discharged', dict(stopJob=0), 'stop_responsibility_unproven')]:
            with self.subTest(case=label), self.assertRaisesRegex(RuntimeError, term):
                stop_handoff(before, [dict(after, **changes)], stop, slot=4)
        # A frame that is still bound is a serving frame and must satisfy the ordinary serving terms.
        bound = sample(recording=0, stopJob=1, epoch=4, frameEpoch=4)
        self.assertEqual(stop_handoff(before, [bound], stop, slot=4),
                         'matching_physical_reader_preserved')
        with self.assertRaisesRegex(RuntimeError, 'rolling_snapshot_unqualified_source'):
            stop_handoff(before, [dict(bound, phase=0)], stop, slot=4)
        # A pre-Stop sample that is already not recording cannot prove the Stop was processed.
        with self.assertRaisesRegex(RuntimeError, 'stop_matching_stop_not_processed'):
            stop_handoff(sample(rawMax=288, recording=0), [after], stop, slot=4)
        # A post-Stop sample that is still recording is not a Stop observation at all (upstream guard).
        with self.assertRaisesRegex(RuntimeError, 'stop_snapshot_invalid'):
            stop_handoff(before, [dict(after, recording=1)], stop, slot=4)

    def test_root_return_completed_is_a_fact_beside_the_label(self):
        """Attempt 8-02's held leg: same-view frozen, then a corrected-role view, then the release.

        The device can serve the frozen take through a corrected-role view first (which is the ordered
        label's `corrected_frozen_handoff`) and still complete the DEC-136 root return afterwards, so the
        recipe requires that fact rather than the label.
        """
        before = sample(period=1536, rawMax=920, liveToken=29, currentRole=1, currentStorage=51)
        stop = dict(raw_length=3544, final_length=1536)
        same_view = sample(recording=0, stopJob=1, phase=0, boundRole=255, boundStorage=0,
                           framePeriod=0, frameOrigin=0, period=1536, sourceOrigin=2,
                           captureToken=29, liveToken=30, currentRole=1, currentStorage=51,
                           rows=1, cursor=4, ledger=0)
        corrected = sample(recording=0, stopJob=1, phase=3, boundRole=0, boundStorage=38,
                           currentRole=0, currentStorage=38, framePeriod=1536, period=1536,
                           sourceOrigin=2, captureToken=29, liveToken=30, rows=1, cursor=1, ledger=0)
        released = sample(recording=0, stopJob=0, phase=0, boundRole=255, boundStorage=0,
                          framePeriod=0, frameOrigin=0, period=1536, sourceOrigin=0, captureToken=0,
                          liveToken=30, currentRole=1, currentStorage=56, rows=1, cursor=2, ledger=0)
        trace = [same_view, corrected, released, dict(released, cursor=1), dict(released, cursor=0)]
        self.assertEqual(stop_handoff(before, trace, stop, slot=4), 'corrected_frozen_handoff')
        self.assertTrue(root_return_completed(before, trace, stop, slot=4))
        self.assertFalse(root_return_completed(before, trace[:2], stop, slot=4))

    def test_stop_truncating_shapes_need_a_successor(self):
        before = sample(rawMax=288)
        mismatch = dict(raw_length=6144, final_length=2304)
        corrected = sample(recording=0, stopJob=1, currentRole=2, currentStorage=10,
                           boundRole=2, boundStorage=10)
        self.assertEqual(stop_handoff(before, [corrected], mismatch, slot=4), 'corrected_frozen_handoff')
        # The same-view reuse shape with no release is the stranding signature, not a handoff.
        reuse = sample(recording=0, stopJob=1, phase=0, boundRole=255, boundStorage=0, framePeriod=0,
                       frameOrigin=0)
        with self.assertRaisesRegex(RuntimeError, 'stop_root_backed_successor_not_observed'):
            stop_handoff(before, [reuse], mismatch, slot=4)
        # A bound frame that does not serve across the playhead is not the frozen serving phase: the
        # corrected handoff needs the serving terms, which the rewind-unbound reuse shape cannot meet.
        unserving = sample(recording=0, stopJob=1, phase=0, currentRole=2, currentStorage=10,
                           boundRole=2, boundStorage=10)
        with self.assertRaisesRegex(RuntimeError, 'stop_frozen_serving_not_observed'):
            stop_handoff(before, [unserving], mismatch, slot=4)

    def test_stop_handoff_dec136_root_return_and_its_discriminators(self):
        """The held leg's terminal, with the pre-route stranding signature kept as the control.

        Shapes are compressed from recorded device evidence: `captures/i5-dec136-releasefix-attempt7-02`
        (route + completion; held leg: two bound samples serving the frozen take through the reuse branch,
        then the committed successor serving with the job released) and
        `captures/i5-rolling-record-approved-full-04` (image 919e17e6; held leg: the job stays bound for
        the whole observation window and no root-backed successor ever takes over).
        """
        before = sample(period=1536, rawMax=920, liveToken=20, currentRole=2, currentStorage=29)
        stop = dict(raw_length=3392, final_length=1536)
        bound = dict(recording=0, stopJob=1, phase=0, boundRole=255, boundStorage=0, framePeriod=0,
                     frameOrigin=0, period=1536, sourceOrigin=2, captureToken=20, liveToken=21,
                     currentRole=2, currentStorage=29, rows=1, cursor=4, ledger=0,
                     firstTick=728, lastTick=728)
        released = dict(recording=0, stopJob=0, phase=0, boundRole=255, boundStorage=0, framePeriod=0,
                        frameOrigin=0, period=1536, sourceOrigin=0, captureToken=0, liveToken=21,
                        currentRole=0, currentStorage=32, rows=1, cursor=1, ledger=0,
                        firstTick=728, lastTick=728)
        route = [sample(**bound), sample(**bound), sample(**released),
                 sample(**dict(released, cursor=2)), sample(**dict(released, cursor=2, ledger=1))]
        self.assertEqual(stop_handoff(before, route, stop, slot=4), 'corrected_frozen_root_return')
        for label, observations, term in [
                ('stranding: job bound for the whole window', [sample(**bound)] * 3,
                 'stop_root_backed_successor_not_observed'),
                ('release while the frozen Capture content still serves',
                 [sample(**bound), sample(**dict(released, sourceOrigin=2, captureToken=20))],
                 'stop_early_release_while_frozen_capture_serves'),
                ('release before the frozen serving phase',
                 [sample(**released), sample(**bound)], 'stop_release_not_terminal'),
                ('unqualified root successor',
                 [sample(**bound), sample(**bound), sample(**dict(released, published=0))],
                 'stop_root_successor_unqualified'),
                ('successor geometry not the frozen take',
                 [sample(**bound), sample(**bound), sample(**dict(released, period=2304))],
                 'stop_root_successor_unqualified'),
                ('frozen receipt never observed while bound', [sample(**dict(bound, captureToken=19))],
                 'stop_frozen_serving_not_observed'),
                ('no observation at all', [], 'stop_observation_missing'),
                ('still recording', [sample(**dict(bound, recording=1))], 'stop_snapshot_invalid')]:
            with self.subTest(case=label), self.assertRaisesRegex(RuntimeError, term):
                stop_handoff(before, observations, stop, slot=4)


    def test_actual_fixture_emitter_matches_oracle_velocity(self):
        from hitl.legacy_edit_baseline import FixtureNote, _emit_fixture_steps_for_clock
        class Port:
            def __init__(self): self.rows = []
            def send(self, message):
                self.rows.append(dict(host_mono=len(self.rows) + 2, **message.dict()))
        port = Port()
        _, next_step, held = _emit_fixture_steps_for_clock(phase_clock=1, step_clocks=6,
            target_bars=1, fixture_by_step={0: [FixtureNote(0, 60, 1)]}, next_emit_step=0,
            out_port=port, ch=7, held=[])
        _emit_fixture_steps_for_clock(phase_clock=7, step_clocks=6, target_bars=1,
            fixture_by_step={}, next_emit_step=next_step, out_port=port, ch=7, held=held)
        self.assertTrue(wire_inventory(port.rows, start=1, end=4, channel=7, pitches=[60])['ok'])
    def test_full_recipe_requires_each_legs_own_terminal(self):
        from hitl.verify.rolling_record import full_recipe_complete

        def legs(**outcomes):
            base = {'growth': 'corrected_frozen_handoff', 'empty': 'corrected_frozen_handoff',
                    'held': 'corrected_frozen_root_return',
                    'matched': 'matching_physical_reader_preserved'}
            base.update(outcomes)
            return [dict(name=name, ok=True, stop_outcome=outcome,
                         stop=dict(root_return_completed=(name == 'held')))
                    for name, outcome in base.items()]

        self.assertTrue(full_recipe_complete(legs()))
        # No leg may borrow another leg's terminal.
        self.assertFalse(full_recipe_complete(legs(matched='corrected_frozen_handoff')))
        # The held leg's requirement is the DEC-136 *fact*, not the ordered label: a corrected-role sample
        # first (measured, attempt 8) still completes the root return.
        self.assertTrue(full_recipe_complete(legs(held='corrected_frozen_handoff')))
        without_root_return = legs()
        next(leg for leg in without_root_return if leg['name'] == 'held')['stop'] = {}
        self.assertFalse(full_recipe_complete(without_root_return))
        self.assertFalse(full_recipe_complete(
            legs(growth='corrected_frozen_root_return', empty='corrected_frozen_root_return')))
        # A missing leg cannot be covered by the others, and a failing leg fails the recipe.
        self.assertFalse(full_recipe_complete([leg for leg in legs() if leg['name'] != 'matched']))
        self.assertFalse(full_recipe_complete([leg for leg in legs() if leg['name'] != 'held']))
        broken = legs()
        broken[1]['ok'] = False
        self.assertFalse(full_recipe_complete(broken))
        # The leg set is a membership requirement: extra legs are allowed (a coverage gap is closed by
        # adding the producing leg) and they must pass too.
        self.assertTrue(full_recipe_complete(legs() + [dict(name='extra', ok=True,
                                                           stop_outcome='corrected_frozen_handoff')]))
        self.assertFalse(full_recipe_complete(legs() + [dict(name='extra', ok=False,
                                                            stop_outcome='corrected_frozen_handoff')]))

    def test_recipe_leg_order_includes_the_non_truncating_stop(self):
        from hitl.scenarios.rolling_record import LEGS
        self.assertEqual(LEGS, ('growth', 'empty', 'held', 'matched'))

    def test_every_leg_path_presses_the_record_stop_once(self):
        """The Stop press is one shared site: a leg path that skips it waits for a stop it never sent.

        Attempt 8-01 on the qualification image failed exactly that way — the growth leg streamed, then
        timed out `rolling_observation_condition_timeout` in the post-stop wait with no
        `Button press: Record/Overdub` row on the wire.
        """
        import inspect
        from hitl.scenarios import rolling_record as scenario
        source = inspect.getsource(scenario._run_leg)
        self.assertEqual(source.count('press_record(action)'), 1)
        self.assertLess(source.index('press_record(action)'),
                        source.index("observe.until(lambda s: not s['recording']"))

    def test_wrong_slot_never_reaches_clear(self):
        from pathlib import Path
        from types import SimpleNamespace
        from unittest.mock import patch, MagicMock
        from hitl.scenarios.rolling_record import _run_leg
        config = SimpleNamespace(loop_slot=4, press_ms=120, midi_channel=8)
        session = SimpleNamespace(out_port=object(), in_port=object())
        capture = SimpleNamespace(session_log=Path('unused'), selected_track_status=lambda **_: dict(track=7, slot=0, state='STOPPED', transport=0))
        collector = SimpleNamespace(snapshot=lambda: [])
        with patch('hitl.scenarios.pit_clock.preflight'), patch('hitl.actions.slots.press_track'), \
             patch('hitl.actions.slots.press_slot_record'), patch('hitl.flows.stopped_preparation.clear_slot') as clear, \
             patch('hitl.actions.observation.await_markers', return_value=True), \
             patch('hitl.midi_clock.MidiClockMaster', return_value=MagicMock()), \
             patch('hitl.scenarios.rolling_record.time.monotonic', side_effect=range(100)):
            with self.assertRaisesRegex(RuntimeError, 'selection_unproven_before_clear'):
                _run_leg('growth', config=config, session=session, capture=capture, collector=collector,
                         observe=None, report={'legs': []}, save=lambda: None)
            clear.assert_not_called()

if __name__ == '__main__':
    unittest.main()
