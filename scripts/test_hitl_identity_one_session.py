import unittest
from types import SimpleNamespace
from hitl_identity_one_session import run_fixture, stream_take, growth_slot_obstacle


class FixtureTests(unittest.TestCase):
    def run_case(self, *, start='RID,start,take=1,t=5,ss=1', after=None,
                 baseline=True, stop=True, auto=False, armed=False, explicit=False):
        args = SimpleNamespace(track=6, loop_slot=2, settle_ms=0, stats_timeout_s=.1,
                               stopped_timeout_s=.1, record_bars=1, bpm=120)
        lines, actions = [], []
        now = [0.0]
        def sleep(n): now[0] += max(n, .01)
        base = 'RID,stats,entered=0,enq=0,rej=0,tx=0,evict=0,take=0'
        final = after or 'RID,stats,entered=2,enq=2,rej=0,tx=1,evict=0,take=1,t=5,ss=1,sp=1,cs=1,pt=5,ps=1,ev=a'
        class Session:
            calls = 0
            def snapshot(self): return list(lines)
            def request_line(self, *a, **kw):
                if a[0] == "!LIB_OBSERVE":
                    rows = [f"#CAP,1,LIBOBS,S,t={t},s={s},k=0,kp=0,kr=0,kc=0,p=1,q=0"
                            for t in range(8) for s in range(8)]
                    return ["#CAP,1,LIBOBS,B,a=1,e=0,s=64", *rows,
                            "#CAP,2,LIBOBS,E,a=1,b=2,s=64,n=1,e=1,v=0,x=1"], True
                if a[0] == "!CAPTURE_CONTENT": return ["CAPTURE_CONTENT,end"], True
                self.calls += 1
                if self.calls == 1: return ([base], True) if baseline else ([], False)
                return [final], True
        def press(note):
            actions.append(note)
            if note == 51 and auto:
                lines.append('#CAP,1,ST,Track,EMPTY,ARMED' if armed else start)
            if note == 36:
                if 'stream' not in actions:
                    lines.append('#CAP,1,ST,Track,EMPTY,ARMED' if armed else start)
                elif stop: lines.append('Recording stopped @ tick 768')
            if note == 39:
                lines.append(start if armed and 'stream' not in actions else 'Transport stopped')
        def start_transport():
            actions.append('MIDI Start')
        def pulse_clock():
            if 'MIDI Start' in actions and 'stream' not in actions:
                lines.append(start)
        def stop_transport():
            actions.append('MIDI Stop')
        result = run_fixture(args, Session(), press, lambda: actions.append('stream'),
                             start_transport=start_transport if explicit else None,
                             stop_transport=stop_transport if explicit else None,
                             pulse_clock=pulse_clock if explicit else None,
                             sleep=sleep, monotonic=lambda: now[0])
        return result, actions

    def test_production_sequence_and_pending_row(self):
        r, a = self.run_case()
        self.assertEqual(a, [65, 51, 36, 'stream', 36, 39])
        self.assertEqual(r['verdict'], 'identity_pass')
        self.assertEqual(r['untransmitted_or_evicted_total'], 1)
        self.assertFalse(r['reboot_allowed'])
        self.assertFalse(r['stored_payload_verified'])

    def test_delayed_ring_start_uses_final_attestation(self):
        r, a = self.run_case(start='[1.0] Recording started @ tick 0')
        self.assertIn('stream', a)
        self.assertTrue(r['start_identity']['provisional'])
        self.assertEqual(r['verdict'], 'identity_pass')
        r, _ = self.run_case(start='[1.0] Recording started @ tick 0',
                            after='RID,stats,entered=2,enq=2,rej=0,tx=1,evict=0,take=2,ev=a')
        self.assertEqual(r['verdict'], 'inconclusive')

    def test_selection_auto_start_not_toggled(self):
        r, a = self.run_case(auto=True)
        self.assertEqual(a, [65, 51, 'stream', 36, 39])
        self.assertEqual(r['verdict'], 'identity_pass')

    def test_armed_requires_transport_start_before_stream(self):
        r, a = self.run_case(armed=True)
        self.assertEqual(a, [65, 51, 36, 39, 'stream', 36, 39])
        self.assertEqual(r['verdict'], 'identity_pass')

    def test_explicit_start_and_clock_never_toggle_transport(self):
        r, a = self.run_case(armed=True, explicit=True)
        self.assertEqual(a, [65, 51, 36, 'MIDI Start', 'stream', 36, 'MIDI Stop'])
        self.assertEqual(r['verdict'], 'identity_pass')
        r, a = self.run_case(armed=True, explicit=True, stop=False)
        self.assertEqual(r['verdict'], 'inconclusive')
        self.assertTrue(r['failure_stop_sent'])
        self.assertEqual(a[-1], 'MIDI Stop')

    def test_slot_selection_can_arm_without_second_record_press(self):
        r, a = self.run_case(auto=True, armed=True, explicit=True)
        self.assertEqual(a, [65, 51, 'MIDI Start', 'stream', 36, 'MIDI Stop'])
        self.assertEqual(r['verdict'], 'identity_pass')

    def test_record_start_refusal_is_named_and_stopped_without_notes(self):
        line = '#MEM_DENIED,1279389986,op=record_start,reason=prewarm_failed,need=0B,heap_free=4KB'
        r, a = self.run_case(start=line, armed=True, explicit=True)
        self.assertEqual(r['reason'], 'record_start_refused')
        self.assertEqual(r['record_start_refusal'], line)
        self.assertNotIn('stream', a)
        self.assertEqual(a[-1], 'MIDI Stop')

    def test_growth_preflight_rejects_populated_pending_and_incomplete(self):
        import copy
        rows = [dict(track=t, slot=s, request='terminal', invalidated=False,
                     content_representation_observed=True, payload='present_verified',
                     has_committed_content=False, loop_length_ticks=0,
                     committed_event_count=0, committed_pass_count=0)
                for t in range(8) for s in range(8)]
        observation = dict(complete=True, slots=rows, window=dict(admission_complete=True, consistent_view=False))
        self.assertIsNone(growth_slot_obstacle(observation, 5, 1))
        for field, value, reason in [('has_committed_content', True, 'target_already_populated'),
                                     ('payload', 'unknown', 'target_payload_unverified'),
                                     ('request', 'queued', 'restore_or_observation_unsettled'),
                                     ('invalidated', True, 'restore_or_observation_unsettled')]:
            bad = copy.deepcopy(observation)
            bad['slots'][41][field] = value
            self.assertEqual(growth_slot_obstacle(bad, 5, 1), reason)
        observation['complete'] = False
        self.assertEqual(growth_slot_obstacle(observation, 5, 1), 'library_observation_incomplete')

    def test_missing_baseline_sends_nothing(self):
        r, a = self.run_case(baseline=False)
        self.assertEqual(a, [])
        self.assertEqual(r['verdict'], 'inconclusive')

    def test_false_or_stale_start_never_streams(self):
        for line in ['Loop 2 selected', 'Overdubbing started', 'RID,start,take=0,t=5,ss=1',
                     'RID,start,take=1,t=5,ss=0']:
            with self.subTest(line=line):
                r, a = self.run_case(start=line)
                self.assertNotIn('stream', a)
                self.assertEqual(r['verdict'], 'inconclusive')

    def test_missing_record_stop_does_not_toggle_transport(self):
        r, a = self.run_case(stop=False)
        self.assertNotIn(39, a)
        self.assertEqual(r['reason'], 'record_stop_unobserved')

    def test_after_identity_must_match_every_boundary(self):
        base = 'RID,stats,entered=2,enq=2,rej=0,tx=1,evict=0,take=1,t=5,ss=1,sp=1,cs=1,pt=5,ps=1,ev=a'
        for old, new in [('take=1','take=2'), ('ps=1','ps=0'), ('sp=1','sp=0'),
                         ('cs=1','cs=0'), ('pt=5','pt=4'), ('ev=a','ev=s')]:
            with self.subTest(old=old):
                r, _ = self.run_case(after=base.replace(old, new))
                self.assertEqual(r['verdict'], 'inconclusive')

    def test_dense_stream_balances_every_note_inside_four_bar_window(self):
        sent=[];now=[0.0]
        def sleep(n):now[0]+=n
        args=SimpleNamespace(track=8,record_bars=4,bpm=120,note_step_clocks=1,
                             note_gate_clocks=1,note_low=0,note_count=128)
        stream_take(args,SimpleNamespace(send=sent.append),lambda kind,**kw:(kind,kw),
                    sleep=sleep,monotonic=lambda:now[0])
        clocks=0;active={};pairs=0;pitches=set()
        for kind,fields in sent:
            if kind=='clock':clocks+=1;continue
            self.assertEqual(fields['channel'],7)
            note=fields['note'];pitches.add(note)
            if kind=='note_on':
                self.assertNotIn(note,active);active[note]=clocks
            else:
                self.assertEqual(clocks-active.pop(note),1);pairs+=1
        self.assertEqual((clocks,pairs,active),(384,383,{}))
        self.assertEqual(pitches,set(range(128)))
        self.assertAlmostEqual(now[0],8.0)

    def test_stream_uses_requested_track_and_balances_notes(self):
        sent = []
        now = [0.0]
        def sleep(n): now[0] += n
        stream_take(SimpleNamespace(track=3, record_bars=1, bpm=120),
                    SimpleNamespace(send=sent.append), lambda kind, **kw: (kind, kw),
                    sleep=sleep, monotonic=lambda: now[0])
        self.assertEqual(sum(k == 'clock' for k, _ in sent), 96)
        self.assertEqual(sum(k == 'note_on' for k, _ in sent), 16)
        self.assertEqual(sum(k == 'note_off' for k, _ in sent), 16)
        self.assertTrue(all(v['channel'] == 2 for k, v in sent if k != 'clock'))
        self.assertAlmostEqual(now[0], 2.0)
        clocks = 0
        starts = {}
        for kind, fields in sent:
            if kind == 'clock': clocks += 1
            if kind == 'note_on': starts[fields['note']] = clocks
            if kind == 'note_off': self.assertEqual(clocks - starts[fields['note']], 2)


if __name__ == '__main__': unittest.main()
