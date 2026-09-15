import unittest
from hitl.verify.stop_restart_survival import verify_restart_survival

class RestartTests(unittest.TestCase):
    def fixture(self,pitches):
        rows=[]
        for cycle in range(3):
            for i,n in enumerate([48]+sorted(set(pitches)-{48})):
                for off in range(2):
                    rows.append({'host_ns':100+cycle*1000+i*2+off,
                                 'message':{'channel':4,'type':'note_off' if off else 'note_on',
                                            'note':n,'velocity':0 if off else 100}})
        return rows
    def test_complete_fixture_survives(self):
        rows=self.fixture(set(range(48,80))|set(range(24,40))|{83})
        result=verify_restart_survival(rows,[{'event':'start','host_ns':0},{'event':'stop','host_ns':3000}])
        self.assertEqual(result['result'],'PASS')
        self.assertEqual(result['complete_cycles'],2)
    def test_base_only_restart_fails_for_all_missing_overdub_notes(self):
        result=verify_restart_survival(self.fixture(range(48,80)),[{'event':'start','host_ns':0},{'event':'stop','host_ns':3000}])
        self.assertEqual(result['result'],'FAIL')
        self.assertEqual(result['cycles'][0]['missing'],list(range(24,40))+[83])
    def test_missing_start_cannot_pass(self):
        self.assertEqual(verify_restart_survival([],[])['result'],'INCONCLUSIVE_CAPTURE')

if __name__=='__main__':unittest.main()
