import copy
import unittest
from hitl_s13_slope import compare_growth, checkpoint, READY

class SlopeTests(unittest.TestCase):
 def test_cross_track_restore_does_not_press_selected_anchor_and_toggle_play(self):
  from unittest.mock import patch
  from hitl_s13_slope import restore_focus
  pressed=[]
  with patch('hitl_s13_slope.time.sleep'),patch('hitl_s13_slope.dump',return_value=[]),patch('hitl_s13_slope.download_slot'),patch('hitl_s13_slope.ram_effective',side_effect=[(13824,[1]),(1536,[1]*62)]):
   restore_focus(None,pressed.append,5,0)
  self.assertEqual(pressed,[64,50,65])
 def test_empty_anchor_requires_stop_before_reading_and_rejects_content(self):
  from unittest.mock import patch
  from hitl_s13_slope import restore_focus
  calls=[]
  with patch('hitl_s13_slope.time.sleep'),patch('hitl_s13_slope.dump',side_effect=lambda s:calls.append('dump') or []),patch('hitl_s13_slope.download_slot'),patch('hitl_s13_slope.ram_effective',side_effect=[(0,[]),(1536,[1]*62)]):
   restore_focus(None,lambda n:calls.append(n),3,1,empty_anchor=True,stop=lambda:calls.append('stop'))
  self.assertEqual(calls,[62,51,'stop','dump',65,'dump'])
  with self.assertRaisesRegex(ValueError,'requires_explicit_stop'):
   restore_focus(None,lambda n:self.fail('pressed before stop guard'),3,1,empty_anchor=True)
  with patch('hitl_s13_slope.time.sleep'),patch('hitl_s13_slope.dump',return_value=[]),patch('hitl_s13_slope.ram_effective',return_value=(768,[1])):
   with self.assertRaisesRegex(ValueError,'gained_content'):
    restore_focus(None,lambda n:None,3,1,empty_anchor=True,stop=lambda:None)
 def test_empty_anchor_preflight_rejects_started_take_or_content_change(self):
  from unittest.mock import patch,Mock
  from hitl_s13_slope import preflight_empty_anchor
  session=Mock();session.request_line.return_value=(['RID,stats,'],True)
  before=self.observation();after=copy.deepcopy(before);after['slots'][0]['committed_event_count']=3
  for entries,observed,error in [([0,1],before,'started_a_take'),([0,0],after,'changed_content')]:
   with patch('hitl_identity_one_session.parse_identity_stats',side_effect=[{'entered':v} for v in entries]),patch('hitl_s13_slope.restore_focus'),patch('hitl_s13_slope.observe',return_value=observed):
    with self.assertRaisesRegex(ValueError,error):preflight_empty_anchor(session,None,None,3,1,before)
 def test_named_memory_denial_is_retained_even_if_operation_later_completes(self):
  from hitl_s13_slope import memory_denials
  lines=['#MEM_DENIED,123,op=snapshot_reserve,reason=heap_low\n',
         '[Memory] ext,refused=0/0B', 'SAVE,completed',
         '#MEM_EXT_REFUSED,125,bytes=32768']
  self.assertEqual(memory_denials(iter(lines)),[lines[0].strip(),lines[3]])
 def test_survival_requires_event_inventory_and_length_not_metadata_hash(self):
  from hitl_s13_slope import compare_survival
  a=dict(length=768,events=[[1,144,6,60,100,1]],event_count=1,sha256='a')
  b=dict(a,sha256='b')
  self.assertFalse(compare_survival(a,b)['file_bytes_equal'])
  for changed in [dict(b,length=1536),dict(b,events=[]),dict(b,events=b['events']*2)]:
   with self.assertRaises(ValueError):compare_survival(a,changed)
 def test_reused_fixture_has_all_required_arguments_before_boot(self):
  from pathlib import Path
  from types import SimpleNamespace
  from hitl_s13_slope import fixture_args
  args=fixture_args(SimpleNamespace(out_dir=Path('capture'),track=5,slot=4,serial_port='/dev/test'))
  self.assertEqual((args.track,args.loop_slot,args.serial_port,args.record_bars),(5,4,'/dev/test',2))
 def test_dense_profile_reaches_the_reused_production_fixture(self):
  from pathlib import Path
  from types import SimpleNamespace
  from hitl_s13_slope import fixture_args
  args=fixture_args(SimpleNamespace(out_dir=Path('capture'),track=8,slot=4,
                   serial_port='/dev/test',profile='dense-four-bar'))
  self.assertEqual((args.record_bars,args.note_step_clocks,args.note_gate_clocks,
                    args.note_low,args.note_count),(4,1,1,0,128))
 def test_restore_anchor_must_be_the_original_populated_selection(self):
  from hitl_s13_slope import original_selected_slot
  obs=self.observation()
  point=dict(boot_roles=['Boot transport t=4 active=0 selected=0 playing=0 enabledMask=0x1'])
  self.assertEqual(original_selected_slot(point,obs,5),0)
  point['boot_roles']=['Boot transport t=4 active=1 selected=1 playing=1 enabledMask=0x2']
  with self.assertRaisesRegex(ValueError,'empty_would_arm'):original_selected_slot(point,obs,5)
  self.assertEqual(original_selected_slot(point,obs,5,allow_empty=True),1)
  with self.assertRaisesRegex(ValueError,'unknown'):original_selected_slot(point,obs,3)
 def observation(self):
  return dict(complete=True,window=dict(admission_complete=True),slots=[dict(track=t,slot=s,
    has_committed_content=(s==0),has_pass_content=(s==0),has_root_content=False,
    loop_length_ticks=768 if s==0 else 0,committed_event_count=2 if s==0 else 0,
    committed_pass_count=1 if s==0 else 0,required_for_initial_playback=(s==0),request='terminal')
    for t in range(8) for s in range(8)])
 def pair(self):
  before=self.observation();after=copy.deepcopy(before)
  next(r for r in after['slots'] if (r['track'],r['slot'])==(5,3))['has_committed_content']=True
  return before,after
 def test_exact_growth(self):
  a,b=self.pair(); self.assertEqual(compare_growth(a,b,(5,3))['loaded_after'],9)
 def test_rejects_eviction_and_role_change_and_existing_content_change(self):
  for field,value in [('has_committed_content',False),('required_for_initial_playback',False),('committed_event_count',7)]:
   a,b=self.pair();b['slots'][0][field]=value
   with self.subTest(field=field),self.assertRaises(ValueError):compare_growth(a,b,(5,3))
 def test_rejects_partial_or_unsettled_observation(self):
  for mutate in [lambda b:b['slots'].pop(),lambda b:b.update(complete=False),lambda b:b['slots'][0].update(request='waiting')]:
   a,b=self.pair();mutate(b)
   with self.assertRaises(ValueError):compare_growth(a,b,(5,3))
 def test_checkpoint_requires_all_readiness_and_footer(self):
  lines=['#CAP,100,DIAG,post_restore_census,free_chunks=449,'+','.join(k+'=1' for k in READY),
         '#MEM_OWNERS,200,live=100KB']+['Boot transport t='+str(t) for t in range(8)]
  self.assertEqual(checkpoint(lines)['readiness']['free_chunks'],449)
  with self.assertRaises(ValueError):checkpoint(lines[:-1])
  for key in READY:
   with self.subTest(key=key),self.assertRaises(ValueError):checkpoint([l.replace(key+'=1',key+'=0') for l in lines])

if __name__=='__main__':unittest.main()
