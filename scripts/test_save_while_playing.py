"""Current-leg Record→PLAYING persistence must complete before any end Stop."""
import unittest
from hitl.verify.save_while_playing import verify_save_while_playing


def rows():
    return [
        '#CAP,101,MI,U,144,16,67,127',
        '#CAP,105,MI,U,250,0,0,0',
        '#CAP,110,RECA,3,0,0',
        '#CAP,120,RECS,stop,3,1536,0,1536,1536,0',
        '#CAP,130,DISP,3,PLAYING,1536,8,8,8,8',
        '#CAP,140,SAVE,in_progress,1',
        '#CAP,150,PERS,work,20,0,0,LoopPersist,slot:7:3,done,ok,write=Header,finalize=Idle',
        '#CAP,160,PERS,result,123,0,0,ok',
        '#CAP,170,SAVE,completed,0',
        '#CAP,200,MI,U,252,0,0,0',
    ]


class SaveWhilePlayingTests(unittest.TestCase):
    def verify(self, lines):
        return verify_save_while_playing(lines, after_us=100, track_index=7, slot_index=3)

    def test_exact_current_record_and_save_before_stop(self):
        result = self.verify(rows())
        self.assertTrue(result['ok'], result)
        self.assertEqual(result['witnesses']['result_us'], 160)
        self.assertEqual(result['witnesses']['transport_stop_us'], 200)

    def test_every_required_receipt_is_required(self):
        for index in (0, 1, 2, 3, 4, 6, 7, 8, 9):
            with self.subTest(index=index):
                r=rows();del r[index]
                self.assertFalse(self.verify(r)['ok'])

    def test_stale_target_result_cannot_satisfy(self):
        r=rows();r[7]='#CAP,99,PERS,result,123,0,0,ok'
        self.assertFalse(self.verify(r)['ok'])

    def test_premature_transport_stop_refuses(self):
        r=rows();r[9]='#CAP,145,MI,U,252,0,0,0'
        self.assertFalse(self.verify(r)['ok'])

    def test_wrong_target_or_length_refuses(self):
        for index, value in ((3,'#CAP,120,RECS,stop,2,1536,0,1536,1536,0'),
                             (3,'#CAP,120,RECS,stop,3,2304,0,2304,2304,0'),
                             (6,'#CAP,150,PERS,work,20,0,0,LoopPersist,slot:7:4,done,ok')):
            r=rows();r[index]=value
            self.assertFalse(self.verify(r)['ok'])

    def test_failed_save_and_completed_after_stop_refuse(self):
        for index,value in ((7,'#CAP,160,PERS,result,123,0,0,failed'),
                            (8,'#CAP,210,SAVE,completed,0')):
            r=rows();r[index]=value
            self.assertFalse(self.verify(r)['ok'])
    def test_target_stops_before_completion_flash_refuses(self):
        r=rows();r.insert(8, '#CAP,165,DISP,3,STOPPED,1536,8,8,8,8')
        self.assertFalse(self.verify(r)['ok'])


    def test_file_order_does_not_replace_device_chronology(self):
        self.assertTrue(self.verify(list(reversed(rows())))['ok'])

    def test_advancing_bar_ticks_prove_running_without_start_receipt(self):
        r=rows();del r[1]
        r.extend(['#CAP,135,BAR,768,1', '#CAP,145,BAR,1536,2'])
        self.assertTrue(self.verify(r)['ok'])

    def test_single_or_stationary_bar_does_not_replace_start_receipt(self):
        for bars in (['#CAP,135,BAR,768,1'],
                     ['#CAP,135,BAR,768,1', '#CAP,145,BAR,768,1']):
            r=rows();del r[1];r.extend(bars)
            self.assertFalse(self.verify(r)['ok'])

    def test_later_success_cannot_replace_failed_first_record_interval(self):
        r=rows()[:4]+['#CAP,130,MI,U,252,0,0,0', '#CAP,305,MI,U,250,0,0,0',
          '#CAP,310,RECA,3,0,0', '#CAP,320,RECS,stop,3,1536,0,1536,1536,0',
          '#CAP,330,DISP,3,PLAYING,1536,8,8,8,8',
          '#CAP,350,PERS,work,20,0,0,LoopPersist,slot:7:3,done,ok',
          '#CAP,360,PERS,result,123,0,0,ok', '#CAP,370,SAVE,completed,0',
          '#CAP,400,MI,U,252,0,0,0']
        self.assertFalse(self.verify(r)['ok'])

    def test_intervening_stopped_frame_cannot_be_hidden_by_resumed_playing(self):
        r=rows();r.extend(['#CAP,135,DISP,3,STOPPED,1536,8,8,8,8',
                           '#CAP,145,DISP,3,PLAYING,1536,8,8,8,8'])
        self.assertFalse(self.verify(r)['ok'])

    def test_no_fresh_floor_refuses(self):
        self.assertFalse(verify_save_while_playing(rows(), after_us=0)['ok'])


if __name__=='__main__':unittest.main()
