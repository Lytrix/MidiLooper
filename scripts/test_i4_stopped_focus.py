"""Guarded stopped focus: current mode/slot/pitch, no blind slot gestures."""
import json
import os
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch
from hitl.scenarios import i4_stopped_focus as focus

A = dict(slot=4, pitch=60, visual_notes=32, loop_length=1536)
B = dict(slot=5, pitch=84, visual_notes=32, loop_length=1536)

def rows(seed=A, start=1_000_000):
    return [f'[{start//1_000_000}.{(start%1_000_000)//1000+1:03d}] [INFO] Edit session: NOTE_EDIT (Program 1, Note 0 trigger)',
            f'#CAP,{start+2000},DISP,{seed["slot"]-1},STOPPED,1536,64,32,32,64,1,0,2,32',
            f'#CAP,{start+3000},DNTE,{seed["pitch"]},0,0,40,0']

def single_poll(source, markers, **kwargs):
    return any(marker in line for line in source() for marker in markers)

class WitnessTests(unittest.TestCase):
    def test_current_complete_focus_witness(self):
        self.assertIsNotNone(focus.focus_witness(rows(),1_000_000,A))
        self.assertIsNotNone(focus.focus_witness(rows(B),1_000_000,B))

    def test_each_mode_display_pitch_endpoint_required(self):
        for i in range(3):
            sample=rows(); del sample[i]
            self.assertIsNone(focus.focus_witness(sample,1_000_000,A))

    def test_stale_wrong_slot_pitch_length_count_or_state_refuses(self):
        self.assertIsNone(focus.focus_witness(rows(),2_000_000,A))
        for before,after in (('DISP,3,','DISP,4,'), ('DNTE,60,','DNTE,84,'),
                             ('STOPPED','PLAYING'),('1536','768'),('64,32,32','64,0,0'),
                             ('64,1,0,2','64,0,0,2')):
            sample=[r.replace(before,after) for r in rows()]
            self.assertIsNone(focus.focus_witness(sample,1_000_000,A),sample)

    def test_newer_wrong_focus_or_mode_cannot_be_hidden(self):
        self.assertIsNone(focus.focus_witness(rows()+['#CAP,1004000,DNTE,84,0,0,40,0'],1_000_000,A))
        self.assertIsNone(focus.focus_witness(rows()+['[1.004] [INFO] Edit session: LOOP_EDIT'],1_000_000,A))

    def test_capture_reset_and_critical_loss_refuse(self):
        for tag in ('RECA,3,0','RING,critical_rejected,1,0','RING,overflow,1',
                    'RING,tier_a_evicted,1','RING,wiped,1','DIAG,boot_reset,crash=0'):
            with self.assertRaises(RuntimeError):focus.focus_witness(rows()+['#CAP,1004000,'+tag],1_000_000,A)

    def test_logger_ack_and_cap_rows_are_time_qualified(self):
        self.assertEqual('NOTE_EDIT',focus.mode_after(rows(),1_000_000))
        self.assertIsNone(focus.mode_after(rows(),2_000_000))
        self.assertEqual(['#CAP,5,DNTE,60,0,0,40,0'],focus.timed_rows(['#CAP,5,DNTE,60,0,0,40,0'],0xfffffff0))

class GuardTests(unittest.TestCase):
    def test_fresh_stopped_target_overlay_required(self):
        status=dict(us=100,track=7,slot=3,state='STOPPED',transport=0)
        cap=SimpleNamespace(selected_track_status=Mock(return_value=status),
                            overlay_status=Mock(return_value=dict(us=101,open=0,phase=0)))
        self.assertEqual(status,focus.stopped(cap,slot=4))
        for change in (dict(track=6),dict(slot=2),dict(state='EMPTY'),dict(state='ARMED'),dict(transport=1)):
            cap.selected_track_status.return_value=dict(status,**change)
            with self.assertRaises(RuntimeError):focus.stopped(cap)
        cap.selected_track_status.return_value=status
        for sample in (None,dict(us=99,open=0,phase=0),dict(us=100,open=0,phase=0),dict(us=101,open=1,phase=0),dict(us=101,open=0,phase=1)):
            cap.overlay_status.return_value=sample
            with self.assertRaises(RuntimeError):focus.stopped(cap)

    def test_old_slot_during_debounce_waits_for_new_slot(self):
        action=SimpleNamespace(collector=Mock()); action.collector.snapshot.return_value=rows(B)
        cap=SimpleNamespace(drain_capture_ring=Mock())
        def twice(source,markers,**kwargs):
            self.assertEqual([],source())
            return any(marker in line for line in source() for marker in markers)
        with patch.object(focus,'stopped',side_effect=[dict(slot=3),dict(slot=4)]) as statuses:
            result=focus.await_focus(action,cap,1_000_000,B,wait=twice)
        self.assertEqual(4,result['status']['slot'])
        self.assertEqual(2,statuses.call_count)

    def test_wait_without_endpoint_cannot_pass(self):
        action=SimpleNamespace(collector=Mock()); action.collector.snapshot.return_value=[]
        with self.assertRaises(RuntimeError):focus.await_focus(action,Mock(),1_000_000,A,wait=Mock(return_value=True))

    def test_seed_target_config_and_qualification_not_waived(self):
        config=dict(track_number=8,first_track_index=7,track_count=1,loop_slot=4,midi_channel=8,
                    record_only=True,record_bars=2,edit_record_fixture=False,record_low_note=60,record_high_note=60)
        with patch.object(focus,'record_seed_qualification_issues',return_value=[]) as qualification:
            self.assertEqual(A,focus.qualified_seed(dict(config=config),4,60))
            qualification.assert_called_once()
            for change in (dict(loop_slot=5),dict(record_low_note=84),dict(track_number=7),dict(record_only=False),dict(edit_record_fixture=True)):
                with self.assertRaises(RuntimeError):focus.qualified_seed(dict(config=dict(config,**change)),4,60)
            qualification.return_value=['incomplete capture']
            with self.assertRaises(RuntimeError):focus.qualified_seed(dict(config=config),4,60)

    def test_panel_reply_is_nonce_session_label_bound(self):
        request=dict(nonce='current',session_log='current.log',label='slot4')
        with tempfile.TemporaryDirectory(dir=os.environ.get('PI_SCRATCH_DIR')) as d:
            path=Path(d)/'reply.json'
            for change in (dict(nonce='old'),dict(session_log='old.log'),dict(label='slot5'),dict(approved=False)):
                path.write_text(json.dumps(dict(request,approved=True,**change))) if 'approved' not in change else path.write_text(json.dumps(dict(request,**change)))
                with self.assertRaises(RuntimeError):focus.await_panel(request,path,Mock(),wait=single_poll)
            path.write_text(json.dumps(dict(request,approved=True)))
            self.assertTrue(focus.await_panel(request,path,Mock(),wait=single_poll)['approved'])

class FlowTests(unittest.TestCase):
    def run_fixture(self,bad_panel=False,changed=False,observe_only=False):
        data=[]; status=dict(us=1_000_000,track=7,slot=4,state='STOPPED',transport=0); mode='LOOP_EDIT'
        action=SimpleNamespace(config=SimpleNamespace(track_number=8,loop_slot=4,press_ms=120),collector=Mock())
        action.collector.snapshot.side_effect=lambda:list(data)
        def stopped(_cap,slot=None):
            status['us']+=1000
            if slot is not None and status['slot']!=slot-1:raise RuntimeError('changed target')
            return dict(status)
        def render():
            seed=A if status['slot']==3 else B
            data.extend(rows(seed,status['us']))
        def normalize(_action):
            data.append('[1.002] [INFO] Edit session: LOOP_EDIT')
        def enter(_action):
            nonlocal mode
            mode='NOTE_EDIT';render()
        def select(_action,slot):
            status['slot']=slot-1
            if mode=='NOTE_EDIT':render()
        def panel(*args,**kwargs):
            if bad_panel:raise RuntimeError('owner refused')
            if changed:status['slot']=4
            return dict(approved=True)
        with tempfile.TemporaryDirectory(dir=os.environ.get('PI_SCRATCH_DIR')) as d, \
             patch.object(focus,'stopped',side_effect=stopped), \
             patch.object(focus,'press_edit_long',side_effect=normalize) as long, \
             patch.object(focus,'press_edit',side_effect=enter) as edit, \
             patch.object(focus,'press_slot_record',side_effect=select) as slots, \
             patch.object(focus,'await_panel',side_effect=panel):
            result=None;error=None
            try:result=focus.run_flow(action,SimpleNamespace(session_log='current.log',drain_capture_ring=Mock()),{4:A,5:B},Path(d),panel_closed=True,observe_only=observe_only,wait=single_poll)
            except RuntimeError as exc:error=str(exc)
            return result,error,[call.args[1] for call in slots.call_args_list],long.call_count,edit.call_count

    def test_safe_full_flow_selects_only_different_filled_slots(self):
        result,error,slots,long,edit=self.run_fixture()
        self.assertIsNone(error)
        self.assertTrue(result['ok'])
        self.assertEqual([4,5,4],slots) # initial LOOP_EDIT selection, then NOTE_EDIT B/A
        self.assertEqual([4,5,4],[epoch['slot'] for epoch in result['epochs']])
        self.assertEqual((1,1),(long,edit))

    def test_owner_refusal_or_changed_slot_stops_before_next_press(self):
        for kwargs in (dict(bad_panel=True),dict(changed=True)):
            result,error,slots,long,edit=self.run_fixture(**kwargs)
            self.assertIsNotNone(error)
            self.assertEqual([4],slots)

    def test_unauthorized_target_or_no_owner_sends_nothing(self):
        action=SimpleNamespace(config=SimpleNamespace(track_number=7,loop_slot=4),collector=Mock())
        with patch.object(focus,'press_edit_long') as send:
            with self.assertRaises(RuntimeError):focus.run_flow(action,Mock(),{4:A,5:B},None,panel_closed=True)
            send.assert_not_called()
class ReviewGuardTests(unittest.TestCase):
    def test_unsafe_press_duration_is_refused_before_any_gesture(self):
        action=SimpleNamespace(config=SimpleNamespace(track_number=8,loop_slot=4,press_ms=900),collector=Mock())
        with patch.object(focus,'stopped',return_value=dict(us=100,slot=4)), \
             patch.object(focus,'press_edit_long') as long, \
             patch.object(focus,'press_edit') as edit, \
             patch.object(focus,'press_slot_record') as slot:
            with self.assertRaisesRegex(RuntimeError,'safe_short_press'):
                focus.run_flow(action,Mock(),{4:A,5:B},None,panel_closed=True,wait=Mock(return_value=False))
            long.assert_not_called();edit.assert_not_called();slot.assert_not_called()



class ProvenanceTests(unittest.TestCase):
    def test_checkpoint_completion_must_precede_both_seed_runs(self):
        cp=dict(started_at=10,completed_at=20,session_log='checkpoint')
        a=dict(started_at=21,completed_at=30,session_log='seed-a')
        b=dict(started_at=31,completed_at=40,session_log='seed-b')
        focus.validate_source_order(cp,a,b)
        for changed in (dict(cp,completed_at=22),dict(cp,completed_at=21)):
            with self.assertRaises(RuntimeError):focus.validate_source_order(changed,a,b)
        with self.assertRaises(RuntimeError):focus.validate_source_order(cp,b,a)
        with self.assertRaises(RuntimeError):focus.validate_source_order(cp,a,dict(b,session_log='seed-a'))

    def make_bundle(self,directory,checkpoint=False):
        from datetime import datetime,timezone
        session=directory/'session.log';session.write_text('test boundary\n')
        shutdown=dict(status='complete',ok=True,drains=2,detail='two drains')
        metadata=dict(session_log=str(session),firmware_hex_sha256=focus.IMAGE_SHA256,
                      timestamp=datetime.fromtimestamp(100,tz=timezone.utc).isoformat())
        report=(dict(session_log=str(session)) if checkpoint else
                dict(config=dict(follow_serial_log=str(session)),assertions=dict(record_evidence_drain=dict(persistence_shutdown=shutdown))))
        report_path=directory/'report.json'
        for path,value in ((report_path,report),(directory/'metadata.json',metadata),(directory/'shutdown-result.json',shutdown)):
            path.write_text(json.dumps(value))
        for path in (session,report_path,directory/'shutdown-result.json'):
            os.utime(path,(101,101))
        return report_path,report,metadata,shutdown

    def test_managed_session_and_embedded_shutdown_bind_one_report(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('PI_SCRATCH_DIR')) as d:
            directory=Path(d);path,report,metadata,shutdown=self.make_bundle(directory)
            actual,link=focus.source_bundle(directory,path)
            self.assertEqual(report,actual);self.assertEqual(100,link['started_at']);self.assertEqual(101,link['completed_at'])
            self.assertEqual(64,len(link['report_sha256']))
            report['config']['follow_serial_log']=str(directory/'different.log')
            path.write_text(json.dumps(report))
            with self.assertRaisesRegex(RuntimeError,'session_or_image'):focus.source_bundle(directory,path)

    def test_mismatched_shutdown_or_image_refuses(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('PI_SCRATCH_DIR')) as d:
            directory=Path(d);path,report,metadata,shutdown=self.make_bundle(directory)
            (directory/'shutdown-result.json').write_text(json.dumps(dict(shutdown,detail='different run')))
            with self.assertRaisesRegex(RuntimeError,'shutdown_report'):focus.source_bundle(directory,path)
            (directory/'shutdown-result.json').write_text(json.dumps(shutdown))
            (directory/'metadata.json').write_text(json.dumps(dict(metadata,firmware_hex_sha256='old-image')))
            with self.assertRaisesRegex(RuntimeError,'session_or_image'):focus.source_bundle(directory,path)

    def test_checkpoint_report_must_link_its_own_session(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('PI_SCRATCH_DIR')) as d:
            directory=Path(d);path,report,metadata,shutdown=self.make_bundle(directory,checkpoint=True)
            self.assertEqual(report,focus.source_bundle(directory,path,checkpoint=True)[0])
            report['session_log']=str(directory/'different.log');path.write_text(json.dumps(report))
            with self.assertRaisesRegex(RuntimeError,'session_or_image'):focus.source_bundle(directory,path,checkpoint=True)


class TimedObservationTests(unittest.TestCase):
    def test_timed_flow_never_claims_physical_pass(self):
        result,error,slots,long,edit=FlowTests().run_fixture(observe_only=True,bad_panel=True)
        self.assertIsNone(error)
        self.assertFalse(result['ok'])
        self.assertTrue(result['machine_ok'])
        self.assertEqual('MACHINE_PASS_OLED_PENDING',result['verdict'])
        self.assertEqual([4,5,4],slots)
        self.assertTrue(all(e['owner_panel_proof'] is None for e in result['epochs']))

    def test_changed_focus_during_hold_refuses(self):
        action=SimpleNamespace(collector=Mock());action.collector.snapshot.side_effect=[rows(),rows(B)]
        def twice(source,markers,**kwargs):
            self.assertEqual(20,kwargs['timeout_s'])
            self.assertEqual([],source())
            source()
            return False
        with patch.object(focus,'stopped',return_value=dict(slot=3)):
            with self.assertRaisesRegex(RuntimeError,'timed_observation'):
                focus.hold_focus(action,Mock(),1_000_000,A,wait=twice)

    def test_observation_without_temp_variant_never_acquires_midi(self):
        args=SimpleNamespace(legacy_args=['--observe-only','--panel-overlay-closed'])
        config=SimpleNamespace(track_number=8,loop_slot=4)
        with patch('hitl.layered_cli.config_from_runner_args',return_value=config), \
             patch('hitl.managed_capture.active_managed_capture',return_value=Mock()), \
             patch('hitl.stimuli.run_stimulus',return_value=SimpleNamespace(kind='canonical')), \
             patch('hitl.session.open_midi_session') as midi:
            self.assertEqual(2,focus.run_i4_stopped_focus(args))
            midi.assert_not_called()


if __name__=='__main__':unittest.main()
