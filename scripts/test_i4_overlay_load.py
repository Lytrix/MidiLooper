"""Guarded managed UI load: no unproven checkpoint or stale owner approval."""
import unittest
from unittest.mock import Mock, patch
from types import SimpleNamespace
from hitl.scenarios.i4_overlay_load import checkpoint_identity, valid_human_proof, fresh_rows, verify_load, run_flow


def reference():
    return {'ok':True,'verdict':'CHECKPOINT_CREATED','stimulus':'STIM-I4-CHECKPOINT-01@v1',
            'checkpoint':{'set_id':1,'revision_id':2,'label':'S0001_v0002'}}


def receipts():
    return ['#CAP,101,LDSV,1', '#CAP,201,PERS,rev_load_dirty_prompt,0,1,2,shown',
            '#CAP,301,PERS,rev_load_dirty_yes,0,1,2,save_then_load',
            '#CAP,302,PERS,rev_request,0,0,0,queued', '#CAP,303,PERS,rev_dispatch,0,0,0,run',
            '#CAP,310,PERS,rev_complete,0,1,3,S0001_v0003',
            '#CAP,311,PERS,rev_load_request,0,1,2,queued', '#CAP,312,PERS,rev_load_dispatch,0,1,2,run',
            '#CAP,320,PERS,rev_load_complete,0,1,2,S0001_v0002', '#CAP,321,LDSV,0']


class OverlayLoadGuards(unittest.TestCase):
    def test_checkpoint_identity_requires_created_matching_proof(self):
        self.assertEqual(checkpoint_identity(reference())['label'],'S0001_v0002')
        for key,value in (('ok',False),('verdict','SETUP_INCOMPLETE'),('stimulus','unknown')):
            r=reference();r[key]=value
            with self.assertRaises(RuntimeError):checkpoint_identity(r)
        r=reference();r['checkpoint']['label']='S0002_v0002'
        with self.assertRaises(RuntimeError):checkpoint_identity(r)

    def test_human_proof_is_nonce_session_identity_bound(self):
        request={'nonce':'abc','session_log':'current.log','label':'S0001_v0002'}
        good=dict(request,approved=True)
        self.assertTrue(valid_human_proof(good,request))
        for key,value in (('nonce','old'),('session_log','previous.log'),('label','S0001_v0001'),('approved',False)):
            reply=dict(good);reply[key]=value
            self.assertFalse(valid_human_proof(reply,request))

    def test_fresh_clock_window_excludes_old_rows_and_handles_rollover(self):
        self.assertEqual(fresh_rows(['#CAP,99,LDSV,1','#CAP,101,LDSV,1'],100),['#CAP,101,LDSV,1'])
        self.assertEqual(fresh_rows(['#CAP,5,LDSV,1'],0xfffffff0),['#CAP,5,LDSV,1'])
        self.assertEqual(fresh_rows(['#CAP,4290000000,LDSV,1'],100000000),[])

    def test_exact_dirty_yes_commit_then_load_autoexit(self):
        result=verify_load(receipts(),reference()['checkpoint'],enter_floor=100,load_floor=200,choice_floor=300)
        self.assertTrue(result['ok'],result)
        self.assertEqual(result['preserving_checkpoint']['label'],'S0001_v0003')

    def test_every_required_load_receipt_required(self):
        for index in range(len(receipts())):
            r=receipts();del r[index]
            self.assertFalse(verify_load(r,reference()['checkpoint'],enter_floor=100,load_floor=200,choice_floor=300)['ok'])

    def test_wrong_load_identity_and_early_exit_refuse(self):
        for index,value in ((8,'#CAP,320,PERS,rev_load_complete,0,2,2,S0002_v0002'),
                            (9,'#CAP,315,LDSV,0'),
                            (5,'#CAP,310,PERS,rev_complete,0,1,2,S0001_v0002')):
            r=receipts();r[index]=value
            self.assertFalse(verify_load(r,reference()['checkpoint'],enter_floor=100,load_floor=200,choice_floor=300)['ok'])

    def test_no_owner_or_unauthorized_target_never_sends(self):
        action=SimpleNamespace(config=SimpleNamespace(track_number=8,loop_slot=4),collector=None)
        with patch('hitl.scenarios.i4_overlay_load.press_edit') as send:
            with self.assertRaises(RuntimeError):run_flow(action,None,reference()['checkpoint'],None,None,panel_closed=True)
            send.assert_not_called()
        action.config.track_number=7
        with self.assertRaises(RuntimeError):run_flow(action,Mock(),reference()['checkpoint'],None,None,panel_closed=True)

    def test_playing_must_be_revalidated_before_overlay_gesture(self):
        action=SimpleNamespace(config=SimpleNamespace(track_number=8,loop_slot=4),collector=Mock())
        action.collector.snapshot.return_value=[]
        playing={'us':100,'track':7,'slot':3,'state':'PLAYING','transport':1}
        stopped=dict(playing,us=110,state='STOPPED')
        capture=SimpleNamespace(overlay_status=Mock(return_value=dict(us=101,open=0)))
        with patch('hitl.scenarios.i4_overlay_load.live_status',side_effect=[playing,stopped]) as statuses, \
             patch('hitl.scenarios.i4_overlay_load.press_edit_double') as enter:
            with self.assertRaisesRegex(RuntimeError,'playing_entry_precondition_changed'):
                run_flow(action,capture,reference()['checkpoint'],None,None,panel_closed=True,wait=Mock(return_value=False))
            self.assertEqual(2,statuses.call_count)
            capture.overlay_status.assert_called_once()
            enter.assert_not_called()

    def test_bad_owner_proof_never_confirms_load(self):
        import tempfile,os
        from pathlib import Path
        collector=Mock();collector.snapshot.return_value=['#CAP,10,LDSV,0']
        action=SimpleNamespace(config=SimpleNamespace(track_number=8,loop_slot=4,press_ms=120),collector=collector)
        with tempfile.TemporaryDirectory(dir=os.environ.get('PI_SCRATCH_DIR')) as directory:
            request=Path(directory)/'request.json';reply=Path(directory)/'reply.json'
            with patch('hitl.scenarios.i4_overlay_load.live_status',return_value={'us':100,'track':7,'slot':3,'state':'PLAYING','transport':1}), \
                 patch('hitl.scenarios.i4_overlay_load.fresh_overlay_snapshot',
                       return_value=dict(us=101, open=0)), \
                 patch('hitl.scenarios.i4_overlay_load.await_overlay_snapshot',
                       return_value=dict(us=101, open=1, mode=2, row=0, set_id=1, phase=0)), \
                 patch('hitl.scenarios.i4_overlay_load.press_edit_double'), \
                 patch('hitl.scenarios.i4_overlay_load.press_overlay_up'), \
                 patch('hitl.scenarios.i4_overlay_load.press_record'), \
                 patch('hitl.scenarios.i4_overlay_load.press_edit_long'), \
                 patch('hitl.scenarios.i4_overlay_load.press_edit') as confirm, \
                 patch('hitl.scenarios.i4_overlay_load.await_identity',side_effect=RuntimeError('wrong proof')) as proof:
                with self.assertRaises(RuntimeError):
                    run_flow(action,Mock(),reference()['checkpoint'],request,reply,panel_closed=True,wait=Mock(return_value=True))
                confirm.assert_not_called()
                proof.assert_called_once()


if __name__=='__main__':unittest.main()
